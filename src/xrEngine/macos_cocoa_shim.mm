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
// No two-stage state, no Escape synthesis. Graceful teardown is the primary
// path for every quit origin (Cmd+Q, Apple menu Quit, Dock Force Quit). An
// earlier revision used a "first press = Esc, second press within 3s =
// _exit(0)" workaround because graceful teardown was triggering a PAC trap
// in xrDebug::Fail on ARM64. That trap is fixed; the workaround was masking
// future regressions of the same teardown path.
//
// There IS a last-resort backstop now, but it is NOT a return to the old
// hack: applicationShouldTerminate: arms a 10-second dispatch_after that
// calls _exit(0) if the graceful path hasn't completed by then. Rationale:
// Dock Force Quit (Option+Quit) on macOS 26.3 calls applicationShouldTerminate:
// (Cocoa graceful), not SIGTERM; our NSTerminateCancel disables Cocoa's own
// SIGKILL timer, and Apple's CoreAnalytics atExitHandler can deadlock in
// malloc_type_calloc during the graceful path, producing a zombie process
// (Dock icon gone, window dead, process alive). The 10s cap caps that
// failure mode without masking ordinary teardown regressions — graceful
// path normally completes in <1s, so the watchdog only fires on a real
// hang. See gitea #61.

#include <SDL.h>
#include <unistd.h>
#include <string.h>

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

// Defined in Engine.cpp under XR_PLATFORM_APPLE — defers KERNEL:disconnect
// then KERNEL:quit on the engine event queue. Out-of-line to keep this .mm
// free of xrCore headers (they collide with Foundation types under ObjC++).
extern "C" void OpenXRay_RequestGracefulQuit(void);

// Lifecycle flag setters defined in Engine.cpp under XR_PLATFORM_APPLE. Each
// stores an atomic pending-event enum which is applied by the render thread at
// the next frame boundary (CRenderDevice::ProcessFrame entry). Decoupling the
// AppKit main-thread notification from the engine thread side-steps the
// NSWorkspace-sync vs render-thread race; observers are level-trigger anyway.
extern "C" void OpenXRay_OnSystemWillSleep(void);
extern "C" void OpenXRay_OnSystemDidWake(void);
extern "C" void OpenXRay_OnAppDidBecomeActive(void);
extern "C" void OpenXRay_OnAppWillResignActive(void);

@interface OpenXRayCocoaShim : NSObject <NSApplicationDelegate>
@property (nonatomic, strong) id sdlDelegate;
- (void)handleQuitRequest:(NSString *)origin;
- (void)workspaceWillSleep:(NSNotification *)note;
- (void)workspaceDidWake:(NSNotification *)note;
@end

// File-scope strong ref so OpenXRay_ArmLifecycleObservers() (called from
// CRenderDevice::Create finalize) can attach NSWorkspace observers after the
// install step ran during SDL bootstrap. Process-lifetime; no unregister.
static OpenXRayCocoaShim *sInstalledShim = nil;

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

// Fallback for menu-driven quit (Apple menu -> Quit) and Dock Force Quit.
// Routes through the same graceful handler as the key monitor.
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
    [self handleQuitRequest:@"appShouldTerminate"];

    // Termination watchdog: NSTerminateCancel below tells Cocoa "I'll
    // handle the quit myself" and disables Cocoa's own SIGKILL timer.
    // The graceful path then runs (eDisconnect -> DestroyLevel -> eQuit
    // -> SDL_QUIT -> ~CApplication -> atexit). On macOS 26.3 Apple's
    // CoreAnalytics atExitHandler can deadlock in malloc_type_calloc,
    // producing a zombie process (icon gone from Dock, window dead).
    // 10-second hard ceiling: if graceful path hasn't reached _exit by
    // then, force it. _exit(0) skips all atexit handlers including
    // the broken CoreAnalytics one. See gitea #61.
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(10 * NSEC_PER_SEC)),
                   dispatch_get_global_queue(QOS_CLASS_BACKGROUND, 0), ^{
        static const char msg[] =
            "==> Cocoa termination watchdog: graceful path timeout (10s), _exit\n";
        ::write(STDERR_FILENO, msg, sizeof(msg) - 1);
        _exit(0);
    });

    return NSTerminateCancel;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
    return NO;
}

// NSWorkspace sleep/wake observers. Notifications arrive on the AppKit main
// thread; we only flip an atomic enum and let the render thread apply it at
// the next ProcessFrame boundary. Engine.cpp handles the actual Pause() call.
- (void)workspaceWillSleep:(NSNotification *)note
{
    (void)note;
    OpenXRay_OnSystemWillSleep();
}

- (void)workspaceDidWake:(NSNotification *)note
{
    (void)note;
    OpenXRay_OnSystemDidWake();
}

// Focus events. Forwarded to SDL's delegate AFTER our hook so SDL's existing
// focus pipeline (SDL_APP_DIDENTERFOREGROUND, mouse cursor restoration) keeps
// working. Apply is also frame-boundary deferred with an idempotency guard,
// so an SDL-originated activate and our Cocoa-originated one collapse to one.
- (void)applicationDidBecomeActive:(NSNotification *)note
{
    OpenXRay_OnAppDidBecomeActive();
    if ([self.sdlDelegate respondsToSelector:_cmd])
    {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
        [(id)self.sdlDelegate performSelector:_cmd withObject:note];
#pragma clang diagnostic pop
    }
}

- (void)applicationWillResignActive:(NSNotification *)note
{
    OpenXRay_OnAppWillResignActive();
    if ([self.sdlDelegate respondsToSelector:_cmd])
    {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
        [(id)self.sdlDelegate performSelector:_cmd withObject:note];
#pragma clang diagnostic pop
    }
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
    static id eventMonitor = nil;
    if (sInstalledShim != nil)
        return; // idempotent

    @autoreleasepool
    {
        NSApplication *app = [NSApplication sharedApplication];
        id existing = [app delegate];
        NSString *existingClass = existing ? NSStringFromClass([existing class]) : @"<nil>";

        sInstalledShim = [[OpenXRayCocoaShim alloc] init];
        sInstalledShim.sdlDelegate = existing;

        // Wrap the SDL delegate so menu-driven quit (Apple menu -> Quit) also
        // hits our graceful handler.
        [app setDelegate:sInstalledShim];

        // Primary intercept: NSEvent local key-down monitor. Fires BEFORE
        // menu/terminate dispatch, so Cmd+Q never reaches SDL's keyboard
        // handler or the menu's terminate: action. Returning nil consumes
        // the event so nothing else sees it.
        __weak OpenXRayCocoaShim *weakShim = sInstalledShim;
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

// Attach NSWorkspace sleep/wake observers AFTER Device.Create() finishes —
// not from OpenXRay_InstallCocoaShim() which runs during early SDL bootstrap.
// Rationale: if we register sleep observers too early, a spurious notification
// landing before the renderer is ready would set the pending-event flag and
// the first ProcessFrame call would apply Pause() against a half-built device.
extern "C" void OpenXRay_ArmLifecycleObservers(void)
{
    if (sInstalledShim == nil)
    {
        static const char msg[] =
            "==> Cocoa shim: ArmLifecycleObservers called before Install — skipped\n";
        ::write(STDERR_FILENO, msg, sizeof(msg) - 1);
        return;
    }

    static BOOL armed = NO;
    if (armed)
        return; // idempotent
    armed = YES;

    @autoreleasepool
    {
        NSNotificationCenter *nc = [[NSWorkspace sharedWorkspace] notificationCenter];
        [nc addObserver:sInstalledShim
               selector:@selector(workspaceWillSleep:)
                   name:NSWorkspaceWillSleepNotification
                 object:nil];
        [nc addObserver:sInstalledShim
               selector:@selector(workspaceDidWake:)
                   name:NSWorkspaceDidWakeNotification
                 object:nil];

        static const char msg[] =
            "==> Cocoa shim: lifecycle observers armed (NSWorkspace sleep/wake)\n";
        ::write(STDERR_FILENO, msg, sizeof(msg) - 1);
    }
}
