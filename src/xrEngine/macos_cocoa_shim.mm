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

#include <atomic>
#include <os/lock.h>

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include "macos_cocoa_shim.h"
#include "Common/PostLogMark.hpp"

// Defined in Engine.cpp under XR_PLATFORM_APPLE — defers KERNEL:disconnect
// then KERNEL:quit on the engine event queue. Out-of-line to keep this .mm
// free of xrCore headers (they collide with Foundation types under ObjC++).
extern "C" void OpenXRay_RequestGracefulQuit(void);

// Defined in xr_input.cpp under XR_PLATFORM_APPLE — pInput->IR_ReleaseAll().
// Used by the focus-loss observer below to drop every held keyboard scancode
// and mouse button: иначе Cmd-Tab при удерживаемой LMB оставляет stuck-fire
// после возврата (NSEvent для button-up в фоновом приложении не приходит).
extern "C" void OpenXRay_SyntheticReleaseAllKeys(void);

// Deferred variant of the above (gitea #134). Sets an atomic pending flag
// drained at the next frame boundary by the render thread (see
// OpenXRay_RunPerFrameMacOSHooks in Engine.cpp). Use this — not the synchronous
// SyntheticReleaseAllKeys — from any AppKit-thread observer: IR_ReleaseAll()
// races KeyUpdate() reading keyboardState on the render thread, and fires
// IR_OnKeyboard{Release} into game receivers that are not thread-safe.
extern "C" void OpenXRay_RequestReleaseAllKeys(void);

// Defined in xr_input.cpp under XR_PLATFORM_APPLE — записывает переданные
// NSEvent.modifierFlags в file-static g_lastShimModifierFlags, чтобы следующий
// FlagsChanged record в NSEventDrain считал diff от актуального состояния.
// Вызывается из applicationDidBecomeActive: после возврата фокуса (пока мы
// были в background, FlagsChanged events не приходили — diff устарел).
extern "C" void OpenXRay_SyncModifierFlags(uint32_t flags);

// Lifecycle flag setters defined in Engine.cpp under XR_PLATFORM_APPLE. Each
// stores an atomic pending-event enum which is applied by the render thread at
// the next frame boundary (CRenderDevice::ProcessFrame entry). Decoupling the
// AppKit main-thread notification from the engine thread side-steps the
// NSWorkspace-sync vs render-thread race; observers are level-trigger anyway.
extern "C" void OpenXRay_OnSystemWillSleep(void);
extern "C" void OpenXRay_OnSystemDidWake(void);
extern "C" void OpenXRay_OnAppDidBecomeActive(void);
extern "C" void OpenXRay_OnAppWillResignActive(void);

// ---------------------------------------------------------------------------
// NSEvent input pipeline (issue #120, A.3 step 2a/4) — infrastructure-only.
//
// Fixed-size ring queue + local-monitor scaffolding for keyboard / mouse /
// scroll / modifier events.
//
// Threading & ordering (issue #132):
//   Producer: NSEvent local-monitor block. Runs on the AppKit main thread
//             during -[NSApplication run] / -[NSApplication sendEvent:],
//             which is also what SDL_PumpEvents drives. Producer touches:
//             buf, head, tail, overflowed.
//   Consumer: OpenXRay_DrainNSEventQueue, called from CInput::OnFrame()
//             (see src/xrEngine/xr_input.cpp:1171). Today the consumer also
//             runs on the main thread (SDL polls events from the same loop
//             that pumps the engine frame), so producer and consumer are
//             temporally — not actually — concurrent. They cannot interleave
//             on the current code path.
//
// Why bother with locking, then? Two reasons:
//   1. Forward compatibility. The native-rewrite endgame (own NSApp +
//      NSWindow + GCController, drop SDL window/input) splits these two
//      onto separate threads. With `nsevent_input=1` becoming default in
//      PR-A (issue #121-family), every keystroke flows through this queue;
//      a partial-write window on Apple Silicon's weak memory model would
//      be hit immediately once the consumer moves off-thread.
//   2. Defence in depth even on single-thread. SDL_PumpEvents can re-enter
//      AppKit during EnableTextInput / DisableTextInput, draining queued
//      events mid-frame (see project memory: SDL_PumpEvents re-entry trap).
//      Treating push/drain as atomic critical sections rules out a
//      half-written record being read by a re-entrant drain.
//
// Implementation: os_unfair_lock around the whole {check-full, write-slot,
// advance-tail} and {read-slot, advance-head} sequences. Chosen over a
// lock-free SPSC ring because the overflow policy is "drop oldest", which
// requires the producer to advance `head` — that violates classic SPSC
// (head is consumer-only). Switching policy to "drop newest" would lose
// release events (key-up, mouse-up), which is the worst possible class to
// drop: a missing release leaves a stuck key. os_unfair_lock is the
// lowest-overhead primitive on Apple platforms for short critical sections
// of this shape and is safe to take from the main thread; it does NOT spin
// in userspace forever, it yields to the kernel after a brief spin.
// ---------------------------------------------------------------------------

namespace
{
constexpr size_t kQueueCapacity = 256;

struct NSEventQueue
{
    OpenXRayNSEventRecord buf[kQueueCapacity];
    size_t        head       = 0; // next read; advanced by consumer, also by
                                  // producer on overflow (drop-oldest policy)
    size_t        tail       = 0; // next write; advanced by producer only
    size_t        count      = 0; // live record count; guarded by lock
    bool          overflowed = false;
    os_unfair_lock lock      = OS_UNFAIR_LOCK_INIT;
};

NSEventQueue g_nsEventQueue;
bool         g_nsEventInputEnabled = true;
bool         g_mouseCaptured       = false;
float        g_backingScaleFactor  = 1.0f;
id           g_nsEventMonitor      = nil;

// Engine-owned text-input gate. Written by CSDLTextInputBackend::Start/Stop
// from CInput::EnableTextInput/DisableTextInput; read by the NSEvent local
// monitor's keyDown/keyUp gate below. Decouples the gate from
// SDL_IsTextInputActive(), which only updates after a pumped SDL event
// loop iteration — the same pump SDL_Start/StopTextInput invokes,
// creating a mid-frame race where the monitor reads stale state during
// [NSTextInputContext activate/deactivate] re-entry.
//
// memory_order_release on write / memory_order_acquire on read is
// required NOT for thread safety (both sides may be main thread today),
// but for proper publication ordering inside the NSApp re-entry window.
std::atomic<bool> g_textInputActive{false};

void QueuePush(const OpenXRayNSEventRecord &rec)
{
    bool justOverflowed = false;
    os_unfair_lock_lock(&g_nsEventQueue.lock);
    {
        if (g_nsEventQueue.count == kQueueCapacity)
        {
            // Drop oldest — preserve newer state (release events more critical
            // than stale presses; losing a key-up leaves a stuck key forever).
            g_nsEventQueue.head = (g_nsEventQueue.head + 1) % kQueueCapacity;
            --g_nsEventQueue.count;

            if (!g_nsEventQueue.overflowed)
            {
                g_nsEventQueue.overflowed = true;
                justOverflowed            = true;
            }
        }
        g_nsEventQueue.buf[g_nsEventQueue.tail] = rec;
        g_nsEventQueue.tail                     = (g_nsEventQueue.tail + 1) % kQueueCapacity;
        ++g_nsEventQueue.count;
    }
    os_unfair_lock_unlock(&g_nsEventQueue.lock);

    // Diagnostic write outside the critical section — ::write can block on a
    // pipe consumer and we don't want a stalled stderr to extend lock hold.
    if (justOverflowed)
    {
        static const char msg[] =
            "==> OpenXRay NSEvent queue overflow — dropping oldest\n";
        ::write(STDERR_FILENO, msg, sizeof(msg) - 1);
    }
}

// Translator helpers — called from the local-monitor handler below.

OpenXRayNSEventRecord MakeRecordFromKey(NSEvent *event, int kind)
{
    OpenXRayNSEventRecord rec = {};
    rec.kind = kind;
    rec.keyCode = (uint16_t)[event keyCode];
    rec.modifierFlags = (uint32_t)([event modifierFlags] &
        NSEventModifierFlagDeviceIndependentFlagsMask);
    rec.isARepeat = [event isARepeat] ? 1 : 0;
    return rec;
}

OpenXRayNSEventRecord MakeRecordFromFlags(NSEvent *event)
{
    OpenXRayNSEventRecord rec = {};
    rec.kind = OXR_NS_EVENT_FLAGS_CHANGED;
    rec.keyCode = (uint16_t)[event keyCode];
    rec.modifierFlags = (uint32_t)([event modifierFlags] &
        NSEventModifierFlagDeviceIndependentFlagsMask);
    return rec;
}

OpenXRayNSEventRecord MakeRecordFromMouse(NSEvent *event, int kind, NSWindow *window)
{
    OpenXRayNSEventRecord rec = {};
    rec.kind = kind;
    rec.modifierFlags = (uint32_t)([event modifierFlags] &
        NSEventModifierFlagDeviceIndependentFlagsMask);

    NSEventType t = [event type];
    if (t == NSEventTypeLeftMouseDown || t == NSEventTypeLeftMouseUp ||
        t == NSEventTypeLeftMouseDragged)
        rec.mouseButton = 0;
    else if (t == NSEventTypeRightMouseDown || t == NSEventTypeRightMouseUp ||
             t == NSEventTypeRightMouseDragged)
        rec.mouseButton = 1;
    else
        rec.mouseButton = 2;

    if (g_mouseCaptured)
    {
        rec.deltaX = (float)[event deltaX];
        rec.deltaY = (float)[event deltaY];
        rec.locX = 0.0f;
        rec.locY = 0.0f;
    }
    else
    {
        NSPoint loc = [event locationInWindow];
        CGFloat windowHeightPt = window
            ? window.contentLayoutRect.size.height
            : [[NSScreen mainScreen] frame].size.height;
        rec.locX = (float)(loc.x * g_backingScaleFactor);
        rec.locY = (float)((windowHeightPt - loc.y) * g_backingScaleFactor);
        rec.deltaX = 0.0f;
        rec.deltaY = 0.0f;
    }
    return rec;
}

OpenXRayNSEventRecord MakeRecordFromScroll(NSEvent *event)
{
    OpenXRayNSEventRecord rec = {};
    rec.kind = OXR_NS_EVENT_SCROLL_WHEEL;
    rec.deltaX = (float)[event scrollingDeltaX];
    rec.deltaY = (float)[event scrollingDeltaY];
    return rec;
}

} // namespace

@interface OpenXRayCocoaShim : NSObject <NSApplicationDelegate>
@property (nonatomic, strong) id sdlDelegate;
- (void)handleQuitRequest:(NSString *)origin;
- (void)workspaceWillSleep:(NSNotification *)note;
- (void)workspaceDidWake:(NSNotification *)note;
- (void)windowDidChangeBackingProperties:(NSNotification *)note;
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

    // Аналогично applicationWillResignActive (A.3.3 Phase 3): отпустить
    // зажатые клавиши и mouse buttons. NSEvent для key-up / mouse-up между
    // sleep и wake не приходят, поэтому держимые на момент засыпания биты
    // останутся залипшими. После wake пользователь начинает с чистого
    // состояния. Defer to frame boundary (gitea #134): IR_ReleaseAll() must
    // not run on the AppKit thread — races KeyUpdate() on the render thread.
    OpenXRay_RequestReleaseAllKeys();
}

- (void)workspaceDidWake:(NSNotification *)note
{
    (void)note;
    OpenXRay_OnSystemDidWake();
}

// NSWindowDidChangeBackingPropertiesNotification — окно переехало между
// мониторами с разной плотностью пикселей (e.g. встроенный 2x Retina ->
// внешний 1x), либо пользователь сменил scale в System Settings. Обновляем
// кэш g_backingScaleFactor чтобы non-captured mouse coords в next NSEvent
// record были корректно scaled.
- (void)windowDidChangeBackingProperties:(NSNotification *)note
{
    NSWindow *window = (NSWindow *)[note object];
    if (!window)
        return;
    const float oldScale = g_backingScaleFactor;
    g_backingScaleFactor = (float)[window backingScaleFactor];

    char buf[160];
    int n = snprintf(buf, sizeof buf,
        "==> OpenXRay: backing scale factor changed %.2f -> %.2f\n",
        (double)oldScale, (double)g_backingScaleFactor);
    if (n > 0)
        ::write(STDERR_FILENO, buf, (size_t)n);
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

    // Sync g_lastShimModifierFlags с реальным состоянием модификаторов.
    // Пока мы были не в фокусе пользователь мог отпустить/нажать Shift,
    // Ctrl, Option и т.п. — NSEvent FlagsChanged события в фоне нам не
    // приходили. Без sync'а следующий FlagsChanged даст некорректный diff
    // и часть модификаторов будет либо stuck-pressed, либо проигнорирована.
    const uint32_t mods = (uint32_t)([NSEvent modifierFlags]
        & NSEventModifierFlagDeviceIndependentFlagsMask);
    OpenXRay_SyncModifierFlags(mods);
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

    // Synthetic release всех зажатых клавиш и mouse buttons. NSEvent local
    // monitor события для background app не получает, поэтому KeyUp/MouseUp
    // от Cmd-Tab выпадают — без этого LMB-зажатие + Cmd-Tab оставляет stuck
    // fire после возврата. IR_ReleaseAll правит и keyboardState и mouseState
    // и эмитит IR_OnKeyboardRelease / IR_OnMouseRelease для всех держимых.
    //
    // Cmd-Tab specifically: macOS delivers the Cmd-down FlagsChanged to us
    // while we are still frontmost, then switches focus before delivering
    // Cmd-up to the new frontmost app — we never see the Cmd-up, so the
    // modifier remains "pressed" in both pipelines (SDL keyboardState and
    // NSEvent g_lastShimModifierFlags). Without release-all on focus loss,
    // returning to the app via Cmd-Tab leaves a phantom Cmd held: the very
    // next bind-capture dialog (Settings -> Key bindings -> any action) snaps
    // the keybind to right Cmd because IR_OnKeyboardHold for RGUI fires from
    // the per-frame hold loop the moment the capture receiver activates.
    //
    // Deferred to frame boundary (gitea #134): IR_ReleaseAll() writes
    // keyboardState/mouseState and dispatches IR_OnKeyboard{Release} into
    // game receivers — both unsafe to do from the AppKit main thread while
    // the render thread is mid-KeyUpdate.
    OpenXRay_RequestReleaseAllKeys();
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

        // A.3 NSEvent input monitor — pass-through stub in commit 2a/4. See
        // namespace block above for queue / translators. Conservative initial
        // backing scale; refined on windowDidChangeBackingProperties later.
        g_backingScaleFactor = (float)[[NSScreen mainScreen] backingScaleFactor];
        OpenXRay_InstallNSEventMonitor();
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

        // NSWindowDidChangeBackingPropertiesNotification приходит через
        // DEFAULT NSNotificationCenter (не NSWorkspace). Регистрируем без
        // object'а — нам интересен любой window, переехавший между мониторами.
        [[NSNotificationCenter defaultCenter] addObserver:sInstalledShim
            selector:@selector(windowDidChangeBackingProperties:)
                name:NSWindowDidChangeBackingPropertiesNotification
              object:nil];

        static const char msg[] =
            "==> Cocoa shim: lifecycle observers armed (NSWorkspace sleep/wake, backing scale)\n";
        ::write(STDERR_FILENO, msg, sizeof(msg) - 1);
    }
}

// ---------------------------------------------------------------------------
// NSEvent input pipeline C entries (issue #120, step 2c/4).
//
// Keyboard / FlagsChanged events are consumed (handler returns nil), so SDL's
// NSApp pump never sees them — engine drains via OpenXRay_DrainNSEventQueue
// from CInput::OnFrame(). Mouse / scroll events are queued but passed through
// (handler returns event); SDL continues to receive them while engine ignores
// the queued copies. Phase 3 flips mouse/scroll to nil-return too.
// ---------------------------------------------------------------------------

extern "C" void OpenXRay_InstallNSEventMonitor(void)
{
    if (g_nsEventMonitor != nil)
        return; // idempotent

    @autoreleasepool
    {
        NSEventMask mask =
            NSEventMaskKeyDown | NSEventMaskKeyUp | NSEventMaskFlagsChanged |
            NSEventMaskMouseMoved |
            NSEventMaskLeftMouseDown  | NSEventMaskLeftMouseUp  | NSEventMaskLeftMouseDragged  |
            NSEventMaskRightMouseDown | NSEventMaskRightMouseUp | NSEventMaskRightMouseDragged |
            NSEventMaskOtherMouseDown | NSEventMaskOtherMouseUp | NSEventMaskOtherMouseDragged |
            NSEventMaskScrollWheel;

        g_nsEventMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:mask
            handler:^NSEvent *(NSEvent *event) {
                NSEventType t = [event type];

                // Cmd+Q sanity. The Cmd+Q monitor installed earlier in
                // OpenXRay_InstallCocoaShim() should fire first and consume,
                // but defend against ordering changes: pass Cmd+Q through
                // untouched so the dedicated monitor (or, fallback, the menu
                // dispatch) handles graceful quit.
                if (t == NSEventTypeKeyDown && [event keyCode] == 0x0C /* kVK_ANSI_Q */ &&
                    ([event modifierFlags] & NSEventModifierFlagCommand))
                {
                    return event;
                }

                if (!g_nsEventInputEnabled)
                    return event; // rollback path (cvar=0)

                NSWindow *window = [event window];

                // Text-input gate (gitea #124, hardened in #141). When SDL
                // text input is active (console open, save-name dialog, MP
                // chat entry), AppKit must see keyDown so SDL can translate
                // it into SDL_TEXTINPUT via the IME / interpretKeyEvents:
                // pipeline. Swallowing the keyDown here (the A.3 default
                // path) blocks SDL_TEXTINPUT entirely and breaks every text
                // entry surface in the engine.
                //
                // When text input is inactive (gameplay, menus driven by
                // discrete key bindings) we still want the A.3 NSEvent
                // pipeline: queue the record and consume so SDL never sees
                // the keyDown — that is the whole reason A.3 exists
                // (deterministic keyCode ordering, no SDL scancode races).
                //
                // The gate reads an engine-owned std::atomic<bool>
                // (g_textInputActive above) published by
                // CSDLTextInputBackend::Start/Stop in SDLTextInputBackend.cpp
                // synchronously with CInput::EnableTextInput/DisableTextInput.
                // The previous SDL_IsTextInputActive() read races
                // [NSTextInputContext activate/deactivate] re-entry inside
                // SDL_Start/StopTextInput (the same pump path drives this
                // monitor), producing stale-state windows that broke console
                // and save-dialog input after the A.6 backend extraction.
                //
                // KeyUp asymmetry — known limitation. If the user holds key
                // X while text input is ACTIVE, then text input toggles off
                // (e.g. console closes) while X is still physically held,
                // the eventual KeyUp arrives with g_textInputActive == false
                // and lands in the A.3 ring queue without a paired KeyDown
                // there — meanwhile SDL keyboardState still sees X as down.
                // Symptom is the stuck-key class; trigger is narrow (modal
                // toggle while key physically held). Deferred to A.7 where
                // the entire keyboard pipeline is unified under native
                // AppKit and the dual-pipeline asymmetry disappears.
                switch (t)
                {
                    case NSEventTypeKeyDown:
                    {
                        if (g_textInputActive.load(std::memory_order_acquire))
                            return event; // let SDL produce SDL_TEXTINPUT
                        QueuePush(MakeRecordFromKey(event, OXR_NS_EVENT_KEY_DOWN));
                        return nil;
                    }
                    case NSEventTypeKeyUp:
                    {
                        if (g_textInputActive.load(std::memory_order_acquire))
                            return event;
                        QueuePush(MakeRecordFromKey(event, OXR_NS_EVENT_KEY_UP));
                        return nil;
                    }
                    case NSEventTypeFlagsChanged:
                        QueuePush(MakeRecordFromFlags(event));
                        return nil;
                    case NSEventTypeMouseMoved:
                        QueuePush(MakeRecordFromMouse(event, OXR_NS_EVENT_MOUSE_MOVE, window));
                        return nil;
                    case NSEventTypeLeftMouseDown:
                    case NSEventTypeRightMouseDown:
                    case NSEventTypeOtherMouseDown:
                        QueuePush(MakeRecordFromMouse(event, OXR_NS_EVENT_MOUSE_DOWN, window));
                        return nil;
                    case NSEventTypeLeftMouseUp:
                    case NSEventTypeRightMouseUp:
                    case NSEventTypeOtherMouseUp:
                        QueuePush(MakeRecordFromMouse(event, OXR_NS_EVENT_MOUSE_UP, window));
                        return nil;
                    case NSEventTypeLeftMouseDragged:
                    case NSEventTypeRightMouseDragged:
                    case NSEventTypeOtherMouseDragged:
                        QueuePush(MakeRecordFromMouse(event, OXR_NS_EVENT_MOUSE_DRAGGED, window));
                        return nil;
                    case NSEventTypeScrollWheel:
                        QueuePush(MakeRecordFromScroll(event));
                        return nil;
                    default:
                        return event;
                }
            }];

        char buf[160];
        int n;
        if (g_nsEventMonitor == nil)
        {
            n = snprintf(buf, sizeof buf,
                "==> OpenXRay: NSEvent input monitor install FAILED\n");
        }
        else
        {
            n = snprintf(buf, sizeof buf,
                "==> OpenXRay: NSEvent input monitor installed (queue capacity %zu)\n",
                kQueueCapacity);
        }
        if (n > 0)
            ::write(STDERR_FILENO, buf, (size_t)n);
    }
}

extern "C" void OpenXRay_SetNSEventInputEnabled(int enabled)
{
    g_nsEventInputEnabled = (enabled != 0);
}

extern "C" void OpenXRay_NotifyTextInputActive(int active)
{
    g_textInputActive.store(active != 0, std::memory_order_release);

    if (active == 0)
    {
        // Bug 7 fix (gitea #155): KeyDown for `~`/console-toggle is swallowed
        // by this monitor (gate=false → A.3 ring → CConsole::Show flips gate
        // true). Subsequent KeyUp arrives with gate=true → forwarded to SDL.
        // SDL never saw the paired KEYDOWN, leaving SDL_GetKeyboardState
        // with a phantom-held key. OS-level autorepeat for that key then
        // floods SDL_TEXTINPUT into the engine queue → IR_OnTextInput spam
        // on whatever IR receiver is on cbStack (rebind UI, gameplay
        // overlay). Reset SDL's view on every gate close so phantom doesn't
        // accumulate. Cost: any physically-held key (W, Shift, ...) at the
        // moment of console close is released from SDL's view and needs a
        // re-press — acceptable trade-off for console-close being rare.
        SDL_ResetKeyboard();
    }
}

extern "C" void OpenXRay_SetMouseCaptureMode(int captured)
{
    g_mouseCaptured = (captured != 0);
}

extern "C" size_t OpenXRay_DrainNSEventQueue(struct OpenXRayNSEventRecord *out, size_t maxCount)
{
    if (out == NULL || maxCount == 0)
        return 0;

    size_t n = 0;
    os_unfair_lock_lock(&g_nsEventQueue.lock);
    {
        while (g_nsEventQueue.count > 0 && n < maxCount)
        {
            out[n++]            = g_nsEventQueue.buf[g_nsEventQueue.head];
            g_nsEventQueue.head = (g_nsEventQueue.head + 1) % kQueueCapacity;
            --g_nsEventQueue.count;
        }
    }
    os_unfair_lock_unlock(&g_nsEventQueue.lock);
    return n;
}
