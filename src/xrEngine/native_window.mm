// Native NSWindow реализация для A.7.4-restart step 1 (gitea #186).
//
// Эта TU компилируется БЕЗ PCH и БЕЗ unity (см. CMakeLists.txt):
// <AppKit/AppKit.h>'s objc.h объявляет BOOL=bool, а xrCore'овский
// Common/PlatformBSD.inl typedef'ит BOOL=int32_t — собранные вместе
// в unity-blob'е они конфликтуют. Тот же приём как у
// `macos_cocoa_shim.mm` и `NativeTextInputBackend.mm`.
//
// Logging: пишем напрямую в stderr через ::write(STDERR_FILENO, ...).
// Это `>> ~/Library/Logs/OpenXRay/openxray.log` через launcher script —
// то же место, что и `POSTLOG_MARK` использует. Не зависит от
// engine'ового `Msg()`, работает на любой фазе жизни (до Core init,
// после Core destroy, при padшем engine'е). Префикс «==> a74p[...]:» —
// единая точка grep'а для step 1.

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <ApplicationServices/ApplicationServices.h>

#include <atomic>
#include <cstdio>
#include <unistd.h>

#include "native_window.h"

#define DLOG(fmt, ...) do {                                                         \
    char _buf[512];                                                                 \
    int _n = snprintf(_buf, sizeof _buf,                                            \
                      "==> a74p[native_window:%s]: " fmt "\n",                      \
                      __func__, ##__VA_ARGS__);                                     \
    if (_n > 0)                                                                     \
        (void)::write(STDERR_FILENO, _buf, (size_t)_n);                             \
} while (0)

// ---------------------------------------------------------------------------
// Passive NSView. NSOpenGLContext setView: будет цепляться к этому.
// `wantsBestResolutionOpenGLSurface = YES` гарантирует, что
// `convertSizeToBacking:` вернёт реальные backing-pixel размеры на retina,
// а не logical points × static 2.
// ---------------------------------------------------------------------------
@interface OXRayNativeContentView : NSView
@end

@implementation OXRayNativeContentView
- (BOOL)isOpaque { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
@end

// ---------------------------------------------------------------------------
// A.7.4 C.4a (gitea #192): per-frame event aggregator. NSWindowDelegate
// callbacks enqueue в last-wins slot per type; engine drains через
// OpenXRay_NativeWindow_PollEvents в OpenXRay_RunPerFrameMacOSHooks.
//
// Атомарных flag'ов достаточно: AppKit зовёт delegate methods на main
// thread (где запускается NSApp run loop, и где SDL_PumpEvents pumps), а
// drain происходит на render thread в начале ProcessFrame. Между ними нет
// shared mutable state кроме этих atomics + size payload, который мы
// guard'им через store-release/load-acquire.
// ---------------------------------------------------------------------------
namespace
{
NSWindow*               g_window = nil;
OXRayNativeContentView* g_view   = nil;

std::atomic<bool> g_pendingResize{false};
std::atomic<int>  g_pendingResizeW{0};
std::atomic<int>  g_pendingResizeH{0};
std::atomic<bool> g_pendingClose{false};
std::atomic<bool> g_pendingActivate{false};
std::atomic<bool> g_pendingActivateValue{false};
std::atomic<bool> g_pendingMinimize{false};
std::atomic<bool> g_pendingMinimizeValue{false};

std::atomic<bool> g_nativeWindowRender{false};

void EnqueueResizeFromCurrent()
{
    if (!g_view)
        return;
    const NSSize logical = [g_view bounds].size;
    const NSSize backing = [g_view convertSizeToBacking:logical];
    g_pendingResizeW.store((int)backing.width, std::memory_order_relaxed);
    g_pendingResizeH.store((int)backing.height, std::memory_order_relaxed);
    g_pendingResize.store(true, std::memory_order_release);
}
} // namespace

// ---------------------------------------------------------------------------
// NSWindowDelegate. Все методы lightweight: только enqueue в atomic slot,
// никакого engine touch'а здесь.
// ---------------------------------------------------------------------------
@interface OXRayNativeWindowDelegate : NSObject<NSWindowDelegate>
@end

@implementation OXRayNativeWindowDelegate
- (void)windowDidResize:(NSNotification*)__unused note
{
    EnqueueResizeFromCurrent();
    DLOG("delegate windowDidResize → resize queued");
}
- (void)windowDidMove:(NSNotification*)__unused note
{
    // Move alone не меняет render size, но Cocoa может слать MOVE без RESIZE
    // когда сдвигаем окно между displays с разным backing scale. На всякий
    // случай enqueue'им resize чтобы Reset подхватил новый scale.
    EnqueueResizeFromCurrent();
}
- (void)windowWillClose:(NSNotification*)__unused note
{
    g_pendingClose.store(true, std::memory_order_release);
    DLOG("delegate windowWillClose → close queued");
}
- (void)windowDidBecomeKey:(NSNotification*)__unused note
{
    g_pendingActivateValue.store(true, std::memory_order_relaxed);
    g_pendingActivate.store(true, std::memory_order_release);
    DLOG("delegate windowDidBecomeKey → activate(true) queued");
}
- (void)windowDidResignKey:(NSNotification*)__unused note
{
    g_pendingActivateValue.store(false, std::memory_order_relaxed);
    g_pendingActivate.store(true, std::memory_order_release);
    DLOG("delegate windowDidResignKey → activate(false) queued");
}
- (void)windowDidMiniaturize:(NSNotification*)__unused note
{
    g_pendingMinimizeValue.store(true, std::memory_order_relaxed);
    g_pendingMinimize.store(true, std::memory_order_release);
    DLOG("delegate windowDidMiniaturize → minimize(true) queued");
}
- (void)windowDidDeminiaturize:(NSNotification*)__unused note
{
    g_pendingMinimizeValue.store(false, std::memory_order_relaxed);
    g_pendingMinimize.store(true, std::memory_order_release);
    DLOG("delegate windowDidDeminiaturize → minimize(false) queued");
}
@end

namespace
{
OXRayNativeWindowDelegate* g_delegate = nil;
} // namespace

extern "C" bool OpenXRay_NativeWindow_Create(int width, int height, const char* title)
{
    DLOG("entry w=%d h=%d title='%s'", width, height, title ? title : "(null)");

    if (g_window)
    {
        DLOG("already created (ptr=%p), idempotent return true", (__bridge void*)g_window);
        return true;
    }

    @autoreleasepool
    {
        const NSWindowStyleMask style = NSWindowStyleMaskTitled |
                                        NSWindowStyleMaskClosable |
                                        NSWindowStyleMaskResizable |
                                        NSWindowStyleMaskMiniaturizable;
        const NSRect rect = NSMakeRect(0, 0, (CGFloat)width, (CGFloat)height);

        DLOG("alloc NSWindow contentRect={0,0,%d,%d} styleMask=0x%lx backing=Buffered defer=NO",
             width, height, (unsigned long)style);
        g_window = [[NSWindow alloc] initWithContentRect:rect
                                               styleMask:style
                                                 backing:NSBackingStoreBuffered
                                                   defer:NO];
        if (!g_window)
        {
            DLOG("FAIL: NSWindow alloc returned nil");
            return false;
        }
        DLOG("NSWindow alloc OK ptr=%p", (__bridge void*)g_window);
        [g_window setReleasedWhenClosed:NO];

        DLOG("alloc OXRayNativeContentView frame={0,0,%d,%d}", width, height);
        g_view = [[OXRayNativeContentView alloc] initWithFrame:rect];
        if (!g_view)
        {
            DLOG("FAIL: contentView alloc returned nil");
            [g_window close];
            g_window = nil;
            return false;
        }
        [g_view setWantsBestResolutionOpenGLSurface:YES];
        DLOG("contentView alloc OK ptr=%p (wantsBestResolutionOpenGLSurface=YES)", (__bridge void*)g_view);

        DLOG("setContentView");
        [g_window setContentView:g_view];

        if (title)
        {
            DLOG("setTitle '%s'", title);
            [g_window setTitle:[NSString stringWithUTF8String:title]];
        }

        // Probe size readouts immediately after create — до того как
        // что-либо trigger resize. Если backing size уже отличается от
        // logical width × 2, то Cocoa авторесайзит или backing scale
        // не 2x — оба факта пригодятся в логе.
        const NSSize logical = [g_view bounds].size;
        const NSSize backing = [g_view convertSizeToBacking:logical];
        DLOG("post-create sizes: logical=%dx%d backing=%dx%d (ratio=%.2fx%.2f)",
             (int)logical.width, (int)logical.height,
             (int)backing.width, (int)backing.height,
             logical.width  > 0 ? backing.width  / logical.width  : -1.0,
             logical.height > 0 ? backing.height / logical.height : -1.0);

        DLOG("done — window is dormant (NOT visible, no makeKeyAndOrderFront)");
    }
    return true;
}

extern "C" void OpenXRay_NativeWindow_Destroy(void)
{
    DLOG("entry");
    if (!g_window)
    {
        DLOG("no window, return");
        return;
    }

    @autoreleasepool
    {
        // Detach delegate before close — windowWillClose: would otherwise fire
        // through our delegate and enqueue a phantom close event for an
        // engine that's already shutting down.
        if (g_delegate)
        {
            DLOG("detach delegate ptr=%p", (__bridge void*)g_delegate);
            [g_window setDelegate:nil];
            g_delegate = nil;
        }
        DLOG("orderOut + close ptr=%p", (__bridge void*)g_window);
        [g_window orderOut:nil];
        [g_window close];
        g_window = nil;
        g_view   = nil;
    }
    DLOG("done");
}

extern "C" void* OpenXRay_NativeWindow_GetNSWindow(void)
{
    return (__bridge void*)g_window;
}

extern "C" void* OpenXRay_NativeWindow_GetContentView(void)
{
    return (__bridge void*)g_view;
}

extern "C" void OpenXRay_NativeWindow_GetBackingSize(int* out_w, int* out_h)
{
    if (!g_view)
    {
        if (out_w) *out_w = 0;
        if (out_h) *out_h = 0;
        DLOG("called before Create: returning 0x0");
        return;
    }
    const NSSize logical = [g_view bounds].size;
    const NSSize backing = [g_view convertSizeToBacking:logical];
    if (out_w) *out_w = (int)backing.width;
    if (out_h) *out_h = (int)backing.height;
    DLOG("logical=%dx%d backing=%dx%d",
         (int)logical.width, (int)logical.height,
         (int)backing.width, (int)backing.height);
}

// ---------------------------------------------------------------------------
// A.7.4c Step C.1 (gitea #190): make the dormant window visible + apply
// SDL-matching collection behavior. Build на existing Create flow.
// ---------------------------------------------------------------------------

extern "C" void OpenXRay_NativeWindow_Show(void)
{
    if (!g_window)
    {
        DLOG("Show: no window, nothing to show");
        return;
    }
    @autoreleasepool
    {
        // A.7.4 C.4a (gitea #192): attach NSWindowDelegate перед make-key, чтобы
        // первый windowDidBecomeKey: захватился в очередь. Идемпотентно: если
        // delegate уже привязан — пропускаем (повторный Show — no-op-style).
        if (!g_delegate)
        {
            g_delegate = [[OXRayNativeWindowDelegate alloc] init];
            [g_window setDelegate:g_delegate];
            DLOG("Show: NSWindowDelegate attached ptr=%p", (__bridge void*)g_delegate);
        }

        // SDL_CreateWindow создаёт SDL'овское окно с focus раньше нашего
        // вызова — без force-activate makeKeyAndOrderFront не делает наше
        // окно key, потому что AppKit держит SDL'овское как key. Force
        // activate + makeKeyWindow:
        DLOG("Show: activateIgnoringOtherApps + makeKeyAndOrderFront ptr=%p",
             (__bridge void*)g_window);
        [NSApp activateIgnoringOtherApps:YES];
        [g_window makeKeyAndOrderFront:nil];
        [g_window makeKeyWindow];
        [g_window makeMainWindow];
        DLOG("Show: post — isVisible=%d isKeyWindow=%d isMainWindow=%d isActive=%d",
             [g_window isVisible], [g_window isKeyWindow], [g_window isMainWindow],
             [NSApp isActive]);
    }
}

extern "C" void OpenXRay_NativeWindow_SetCollectionBehavior(unsigned long behavior)
{
    if (!g_window)
    {
        DLOG("SetCollectionBehavior: no window, ignore (behavior=0x%lx)", behavior);
        return;
    }
    @autoreleasepool
    {
        const NSWindowCollectionBehavior current = [g_window collectionBehavior];
        const NSWindowCollectionBehavior next = current | (NSWindowCollectionBehavior)behavior;
        DLOG("SetCollectionBehavior: 0x%lx |= 0x%lx → 0x%lx",
             (unsigned long)current, behavior, (unsigned long)next);
        [g_window setCollectionBehavior:next];
    }
}

extern "C" void OpenXRay_NativeWindow_SetTitle(const char* utf8Title)
{
    if (!g_window || !utf8Title)
    {
        DLOG("SetTitle: no window or null title, ignore");
        return;
    }
    @autoreleasepool
    {
        DLOG("SetTitle '%s'", utf8Title);
        [g_window setTitle:[NSString stringWithUTF8String:utf8Title]];
    }
}

// ---------------------------------------------------------------------------
// A.7.4 C.4a (gitea #192): extended C-API. Все null-safe.
// ---------------------------------------------------------------------------

extern "C" void OpenXRay_NativeWindow_SetMinimumSize(int w, int h)
{
    if (!g_window)
    {
        DLOG("SetMinimumSize: no window, ignore (%dx%d)", w, h);
        return;
    }
    @autoreleasepool
    {
        DLOG("SetMinimumSize %dx%d points", w, h);
        [g_window setContentMinSize:NSMakeSize((CGFloat)w, (CGFloat)h)];
    }
}

extern "C" void OpenXRay_NativeWindow_GetClientRect(int* x, int* y, int* w, int* h)
{
    if (x) *x = 0;
    if (y) *y = 0;
    if (w) *w = 0;
    if (h) *h = 0;
    if (!g_view)
        return;
    @autoreleasepool
    {
        const NSRect bounds = [g_view bounds];
        if (x) *x = (int)bounds.origin.x;
        if (y) *y = (int)bounds.origin.y;
        if (w) *w = (int)bounds.size.width;
        if (h) *h = (int)bounds.size.height;
    }
}

extern "C" void OpenXRay_NativeWindow_GetFrameRect(int* x, int* y, int* w, int* h)
{
    if (x) *x = 0;
    if (y) *y = 0;
    if (w) *w = 0;
    if (h) *h = 0;
    if (!g_window)
        return;
    @autoreleasepool
    {
        const NSRect frame = [g_window frame];
        if (x) *x = (int)frame.origin.x;
        if (y) *y = (int)frame.origin.y;
        if (w) *w = (int)frame.size.width;
        if (h) *h = (int)frame.size.height;
    }
}

extern "C" bool OpenXRay_NativeWindow_IsKeyWindow(void)
{
    if (!g_window)
        return false;
    @autoreleasepool
    {
        return [g_window isKeyWindow] ? true : false;
    }
}

extern "C" bool OpenXRay_NativeWindow_IsMinimized(void)
{
    if (!g_window)
        return false;
    @autoreleasepool
    {
        return [g_window isMiniaturized] ? true : false;
    }
}

extern "C" void OpenXRay_NativeWindow_WarpCursorInWindow(int x, int y)
{
    if (!g_window)
    {
        DLOG("WarpCursorInWindow: no window, ignore (%d,%d)", x, y);
        return;
    }
    @autoreleasepool
    {
        // Cocoa: point (x,y) — origin top-left of contentView in points.
        // NSWindow convertRectToScreen expects rect в window-coords (origin
        // bottom-left для NSWindow). Сначала flip y относительно contentView
        // bounds, затем convert through window.
        const NSRect bounds = [g_view bounds];
        const CGFloat flippedY = bounds.size.height - (CGFloat)y;
        const NSRect localRect = NSMakeRect((CGFloat)x, flippedY, 1, 1);
        const NSRect screenRect = [g_window convertRectToScreen:localRect];

        // CGWarpMouseCursorPosition expects top-left origin screen coords.
        // NSScreen mainScreen frame высота даёт нам нужный flip baseline.
        const NSRect mainScreenFrame = [[[NSScreen screens] firstObject] frame];
        const CGFloat cgY = mainScreenFrame.size.height - screenRect.origin.y;
        const CGPoint cgPos = CGPointMake(screenRect.origin.x, cgY);

        CGWarpMouseCursorPosition(cgPos);
        // CGAssociateMouseAndMouseCursorPosition не вызываем — caller
        // (xr_input.cpp) делает это отдельно через SetCursorAssociated.
    }
}

extern "C" void OpenXRay_NativeWindow_SetCursorAssociated(bool associated)
{
    // CGAssociateMouseAndMouseCursorPosition сам по себе не требует окна;
    // это process-wide tether флаг. Окей даже когда g_window == nil
    // (хотя caller всё равно gating'ует через Device.m_useNativeWindow).
    DLOG("SetCursorAssociated %d", associated ? 1 : 0);
    CGAssociateMouseAndMouseCursorPosition(associated ? 1 : 0);
}

// ---------------------------------------------------------------------------
// A.7.4 C.4a: per-frame event drain. Trampolines'ы определены в Engine.cpp
// и зовут engine'овский state (Reset, OnWindowActivate, Pause, graceful
// shutdown). Здесь — только drain atomic'ов + invoke.
// ---------------------------------------------------------------------------

extern "C" void OpenXRay_NativeWindow_PollEvents(
    void (*on_resize)(int w, int h),
    void (*on_close)(void),
    void (*on_activate)(bool active),
    void (*on_minimize)(bool minimized))
{
    if (g_pendingResize.exchange(false, std::memory_order_acq_rel))
    {
        const int w = g_pendingResizeW.load(std::memory_order_relaxed);
        const int h = g_pendingResizeH.load(std::memory_order_relaxed);
        if (on_resize)
            on_resize(w, h);
    }
    if (g_pendingActivate.exchange(false, std::memory_order_acq_rel))
    {
        const bool active = g_pendingActivateValue.load(std::memory_order_relaxed);
        if (on_activate)
            on_activate(active);
    }
    if (g_pendingMinimize.exchange(false, std::memory_order_acq_rel))
    {
        const bool minimized = g_pendingMinimizeValue.load(std::memory_order_relaxed);
        if (on_minimize)
            on_minimize(minimized);
    }
    if (g_pendingClose.exchange(false, std::memory_order_acq_rel))
    {
        if (on_close)
            on_close();
    }
}

extern "C" bool OpenXRay_IsNativeWindowRender(void)
{
    return g_nativeWindowRender.load(std::memory_order_acquire);
}

extern "C" void OpenXRay_SetNativeWindowRender(bool enabled)
{
    DLOG("SetNativeWindowRender %d", enabled ? 1 : 0);
    g_nativeWindowRender.store(enabled, std::memory_order_release);
}
