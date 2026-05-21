// macOS Cocoa shim for OpenXRay.
//
// Cmd+Q on macOS normally dispatches through the Application menu's Quit item
// to [NSApp terminate:], which calls applicationShouldTerminate: on the
// NSApplicationDelegate. SDL2 installs its own delegate that responds by
// posting SDL_QUIT directly. We need a Cmd+Q that walks the same path as the
// in-game "Quit to Windows" button, so net_Stop/DestroyLevel run before
// SDL_QUIT is pushed — otherwise the engine tears down the GL context while
// level state is still live and the process wedges with no user recovery.
//
// We intercept Cmd+Q at TWO layers and route both to the same handler:
//
//   1. NSEvent local key-down monitor. Fires BEFORE the menu/terminate
//      dispatch, regardless of which delegate AppKit thinks it should call.
//      Reliable across SDL versions and SDL delegate replacements.
//
//   2. applicationShouldTerminate: override on a wrapped delegate. Fallback
//      for menu-driven quit (Apple menu -> Quit OpenXRay) where no key-down
//      event fires.
//
// Both paths defer KERNEL:disconnect then KERNEL:quit on the engine's event
// queue. This is exactly what `CCC_Quit::Execute` (xr_ioc_cmd.cpp) and the
// SDL_WINDOWEVENT_CLOSE handler (device.cpp) already do — verified to
// teardown cleanly in any state (in-game, paused, main menu).
//
// No two-stage state, no _exit(0) backstop, no Escape synthesis. An earlier
// revision used a "first press = Esc, second press within 3s = _exit(0)"
// workaround because graceful teardown was triggering a PAC trap in
// xrDebug::Fail on ARM64. That trap is fixed; the workaround was masking
// future regressions of the same teardown path. If teardown ever hangs
// again it's a bug to root-cause, not to paper over — macOS-level Force
// Quit (Cmd+Opt+Esc, Activity Monitor) is the user fallback, not in-app
// self-kill.

#include <SDL.h>
#include <unistd.h>
#include <string.h>

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

// Defined in Engine.cpp under XR_PLATFORM_APPLE — defers KERNEL:disconnect
// then KERNEL:quit on the engine event queue. Out-of-line to keep this .mm
// free of xrCore headers (they collide with Foundation types under ObjC++).
extern "C" void OpenXRay_RequestGracefulQuit(void);

@interface OpenXRayCocoaShim : NSObject <NSApplicationDelegate>
@property (nonatomic, strong) id sdlDelegate;
- (void)handleQuitRequest:(NSString *)origin;
@end

@implementation OpenXRayCocoaShim

- (void)handleQuitRequest:(NSString *)origin
{
    char buf[160];
    int n = snprintf(buf, sizeof buf,
        "==> Cocoa shim: Cmd+Q -> graceful quit (via %s)\n",
        origin.UTF8String);
    if (n > 0)
        ::write(STDERR_FILENO, buf, (size_t)n);

    OpenXRay_RequestGracefulQuit();
}

// Fallback for menu-driven quit (Apple menu -> Quit). Routes through the same
// graceful handler as the key monitor.
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

        // Wrap the SDL delegate so menu-driven quit (Apple menu -> Quit) also
        // hits our graceful handler.
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
