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

namespace
{
NSWindow*               g_window = nil;
OXRayNativeContentView* g_view   = nil;
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
