// macOS Cocoa shim for OpenXRay.
//
// Cmd+Q on macOS normally dispatches through the Application menu's Quit item
// to [NSApp terminate:], which calls applicationShouldTerminate: on the
// NSApplicationDelegate. SDL2 installs its own delegate that responds by
// posting SDL_QUIT, which drives the engine into Device.Shutdown() and the GL
// teardown path. On the current macOS GL stack, that teardown either crashes
// (PAC trap in xrDebug::Fail) or wedges the GPU driver (uninterruptible TX
// state). Neither is acceptable as a user-facing quit gesture.
//
// We intercept Cmd+Q at TWO layers, primary first:
//
//   1. NSEvent local key-down monitor. This fires BEFORE the menu/terminate
//      dispatch, regardless of which delegate AppKit thinks it should call.
//      Reliable across SDL versions and SDL delegate replacements.
//
//   2. applicationShouldTerminate: override on a wrapped delegate. This is the
//      fallback for menu-driven quit (e.g. Apple menu -> Quit OpenXRay) where
//      no key-down event fires.
//
// Both paths call -handleQuitRequest: on the same shim instance, implementing
// a two-stage behavior:
//
//   Stage 1 — first Cmd+Q: synthesize SDL_KEYDOWN+SDL_KEYUP for Escape. The
//   in-game pause menu opens; the user can exit via "Quit to Windows", which
//   is the vanilla code path the engine actually tests.
//
//   Stage 2 — second Cmd+Q within 3 s: call _exit(0). macOS reaps the process
//   immediately, no teardown, no dialogs. Unsaved state is lost — this is a
//   deliberate trade for stability over save data. Documented in the bundle
//   README and notes/macos-build-guide.md.
//
// After the 3 s window elapses without a second press, the counter resets and
// the next Cmd+Q is treated as a fresh "first press".
//
// Caveat: if the main thread is wedged in the GPU driver (typical TX state
// after a load hang), neither path helps — our key monitor and delegate both
// run on the main thread. windowed mode is the complementary safety net for
// that case: a regular NSWindow stays addressable by Cmd+Opt+Esc Force Quit
// even when the app's own event loop is dead.

#include <SDL.h>
#include <unistd.h>
#include <string.h>

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

@interface OpenXRayCocoaShim : NSObject <NSApplicationDelegate>
@property (nonatomic, strong) id sdlDelegate;
@property (nonatomic, assign) NSTimeInterval lastQuitRequestTime;
- (void)handleQuitRequest:(NSString *)origin;
@end

@implementation OpenXRayCocoaShim

- (void)handleQuitRequest:(NSString *)origin
{
    NSTimeInterval now = [NSDate timeIntervalSinceReferenceDate];
    NSTimeInterval delta = now - self.lastQuitRequestTime;
    const NSTimeInterval HARD_EXIT_WINDOW = 3.0;

    if (self.lastQuitRequestTime > 0.0 && delta <= HARD_EXIT_WINDOW)
    {
        // Stage 2: hard exit. Bypass everything.
        char buf[128];
        int n = snprintf(buf, sizeof buf, "==> Cocoa shim: hard exit (second Cmd+Q via %s)\n", origin.UTF8String);
        if (n > 0)
            ::write(STDERR_FILENO, buf, (size_t)n);
        ::_exit(0);
    }

    // Stage 1: synthesize Escape so the in-game pause menu opens.
    self.lastQuitRequestTime = now;

    SDL_Event keydown;
    SDL_zero(keydown);
    keydown.type = SDL_KEYDOWN;
    keydown.key.state = SDL_PRESSED;
    keydown.key.repeat = 0;
    keydown.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
    keydown.key.keysym.sym = SDLK_ESCAPE;
    keydown.key.keysym.mod = KMOD_NONE;
    SDL_PushEvent(&keydown);

    SDL_Event keyup;
    SDL_zero(keyup);
    keyup.type = SDL_KEYUP;
    keyup.key.state = SDL_RELEASED;
    keyup.key.repeat = 0;
    keyup.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
    keyup.key.keysym.sym = SDLK_ESCAPE;
    keyup.key.keysym.mod = KMOD_NONE;
    SDL_PushEvent(&keyup);

    char buf[160];
    int n = snprintf(buf, sizeof buf,
        "==> Cocoa shim: Cmd+Q -> Esc (via %s; press Cmd+Q again within 3s to hard-exit)\n",
        origin.UTF8String);
    if (n > 0)
        ::write(STDERR_FILENO, buf, (size_t)n);

    // Reset the counter after the window expires.
    __weak OpenXRayCocoaShim *weakSelf = self;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(HARD_EXIT_WINDOW * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
        OpenXRayCocoaShim *strongSelf = weakSelf;
        if (!strongSelf)
            return;
        NSTimeInterval cur = [NSDate timeIntervalSinceReferenceDate];
        if (cur - strongSelf.lastQuitRequestTime >= HARD_EXIT_WINDOW - 0.01)
            strongSelf.lastQuitRequestTime = 0.0;
    });
}

// Fallback for menu-driven quit (Apple menu -> Quit). Routes through the same
// two-stage handler as the key monitor.
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
    [self handleQuitRequest:@"appShouldTerminate"];
    return NSTerminateCancel;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
    return NO;
}

// Transparent forwarding to SDL's delegate for everything we don't override.
- (BOOL)respondsToSelector:(SEL)aSelector
{
    if ([super respondsToSelector:aSelector])
        return YES;
    return [self.sdlDelegate respondsToSelector:aSelector];
}

- (id)forwardingTargetForSelector:(SEL)aSelector
{
    if ([self.sdlDelegate respondsToSelector:aSelector])
        return self.sdlDelegate;
    return nil;
}

@end

extern "C" void OpenXRay_InstallCocoaShim(void)
{
    static OpenXRayCocoaShim *installed = nil;
    static id eventMonitor = nil;
    if (installed != nil)
        return; // idempotent

    @autoreleasepool
    {
        NSApplication *app = [NSApplication sharedApplication];
        id existing = [app delegate];
        NSString *existingClass = existing ? NSStringFromClass([existing class]) : @"<nil>";

        installed = [[OpenXRayCocoaShim alloc] init];
        installed.sdlDelegate = existing;
        installed.lastQuitRequestTime = 0.0;

        // Wrap the SDL delegate so menu-driven quit (Apple menu -> Quit) also
        // hits our two-stage handler.
        [app setDelegate:installed];

        // Primary intercept: NSEvent local key-down monitor. Fires BEFORE
        // menu/terminate dispatch, so Cmd+Q never reaches SDL's keyboard
        // handler or the menu's terminate: action. Returning nil consumes
        // the event so nothing else sees it.
        __weak OpenXRayCocoaShim *weakShim = installed;
        eventMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
            handler:^NSEvent *(NSEvent *event) {
                NSUInteger mods = event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
                BOOL cmdHeld = (mods & NSEventModifierFlagCommand) != 0;
                if (!cmdHeld)
                    return event;
                NSString *chars = event.charactersIgnoringModifiers;
                if (chars.length == 1 && [chars characterAtIndex:0] == 'q')
                {
                    OpenXRayCocoaShim *strong = weakShim;
                    if (strong)
                        [strong handleQuitRequest:@"keyMonitor"];
                    return nil; // consume; do not dispatch further
                }
                return event;
            }];

        // Stop App Nap so the runloop stays responsive enough to keep the key
        // monitor firing when the app is backgrounded.
        [[NSProcessInfo processInfo] disableAutomaticTermination:@"OpenXRay running"];

        char buf[256];
        int n = snprintf(buf, sizeof buf,
            "==> Cocoa shim: installed (wrapped SDL delegate=%s; NSEvent key monitor=%s)\n",
            existingClass.UTF8String,
            eventMonitor ? "active" : "FAILED");
        if (n > 0)
            ::write(STDERR_FILENO, buf, (size_t)n);
    }
}
