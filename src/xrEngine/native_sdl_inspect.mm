// SDL inspection реализация — A.7.4b Step B.1 (gitea #188).
//
// Компилируется БЕЗ PCH и БЕЗ unity (см. CMakeLists.txt) по той же
// причине что native_window.mm — AppKit BOOL clash.

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <SDL.h>
#include <SDL_syswm.h>

#include <cstdio>
#include <unistd.h>

#include "native_sdl_inspect.h"

#define DLOG(fmt, ...) do {                                                         \
    char _buf[512];                                                                 \
    int _n = snprintf(_buf, sizeof _buf,                                            \
                      "==> a74p[sdl_inspect:%s]: " fmt "\n",                        \
                      __func__, ##__VA_ARGS__);                                     \
    if (_n > 0)                                                                     \
        (void)::write(STDERR_FILENO, _buf, (size_t)_n);                             \
} while (0)

namespace
{
const char* PixelFormatAttrName(NSOpenGLPixelFormatAttribute a)
{
    switch (a)
    {
    case NSOpenGLPFAColorSize:        return "ColorSize";
    case NSOpenGLPFAAlphaSize:        return "AlphaSize";
    case NSOpenGLPFADepthSize:        return "DepthSize";
    case NSOpenGLPFAStencilSize:      return "StencilSize";
    case NSOpenGLPFADoubleBuffer:     return "DoubleBuffer";
    case NSOpenGLPFAAccelerated:      return "Accelerated";
    case NSOpenGLPFANoRecovery:       return "NoRecovery";
    case NSOpenGLPFASampleBuffers:    return "SampleBuffers";
    case NSOpenGLPFASamples:          return "Samples";
    case NSOpenGLPFAOpenGLProfile:    return "OpenGLProfile";
    case NSOpenGLPFAStereo:           return "Stereo";
    case NSOpenGLPFAAuxBuffers:       return "AuxBuffers";
    case NSOpenGLPFAMinimumPolicy:    return "MinimumPolicy";
    case NSOpenGLPFAMaximumPolicy:    return "MaximumPolicy";
    case NSOpenGLPFAAllRenderers:     return "AllRenderers";
    case NSOpenGLPFABackingStore:     return "BackingStore";
    case NSOpenGLPFAClosestPolicy:    return "ClosestPolicy";
    case NSOpenGLPFAMultisample:      return "Multisample";
    case NSOpenGLPFASupersample:      return "Supersample";
    case NSOpenGLPFASampleAlpha:      return "SampleAlpha";
    case NSOpenGLPFAOffScreen:        return "OffScreen";
    case NSOpenGLPFAColorFloat:       return "ColorFloat";
    default:                          return "?";
    }
}

void DumpPixelFormat(NSOpenGLPixelFormat* pf)
{
    if (!pf)
    {
        DLOG("  pixel format is nil");
        return;
    }
    static const NSOpenGLPixelFormatAttribute attrs[] = {
        NSOpenGLPFAColorSize, NSOpenGLPFAAlphaSize,
        NSOpenGLPFADepthSize, NSOpenGLPFAStencilSize,
        NSOpenGLPFADoubleBuffer, NSOpenGLPFAAccelerated,
        NSOpenGLPFANoRecovery, NSOpenGLPFASampleBuffers,
        NSOpenGLPFASamples, NSOpenGLPFAOpenGLProfile,
        NSOpenGLPFAStereo, NSOpenGLPFAAuxBuffers,
        NSOpenGLPFABackingStore, NSOpenGLPFAClosestPolicy,
        NSOpenGLPFAMultisample, NSOpenGLPFASupersample,
        NSOpenGLPFASampleAlpha, NSOpenGLPFAColorFloat,
    };
    GLint value = 0;
    for (NSOpenGLPixelFormatAttribute a : attrs)
    {
        [pf getValues:&value forAttribute:a forVirtualScreen:0];
        DLOG("  PixelFormat[%s]=%d", PixelFormatAttrName(a), (int)value);
    }
}
} // namespace

extern "C" void OpenXRay_NativeSDLInspect_Window(SDL_Window* sdlWnd)
{
    DLOG("entry sdlWnd=%p", (void*)sdlWnd);
    if (!sdlWnd)
    {
        DLOG("sdlWnd is null, abort");
        return;
    }

    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(sdlWnd, &info))
    {
        DLOG("SDL_GetWindowWMInfo failed: %s", SDL_GetError());
        return;
    }
    if (info.subsystem != SDL_SYSWM_COCOA)
    {
        DLOG("info.subsystem != COCOA (%d) — abort", (int)info.subsystem);
        return;
    }

    @autoreleasepool
    {
        NSWindow* nsWnd = info.info.cocoa.window;
        if (!nsWnd)
        {
            DLOG("info.cocoa.window is nil");
            return;
        }

        DLOG("NSWindow ptr=%p class=%s",
             (__bridge void*)nsWnd,
             NSStringFromClass([nsWnd class]).UTF8String);
        const NSRect frame = [nsWnd frame];
        DLOG("  frame=(%g,%g %gx%g)", frame.origin.x, frame.origin.y,
             frame.size.width, frame.size.height);
        DLOG("  styleMask=0x%lx level=%ld isVisible=%d isKeyWindow=%d isMainWindow=%d",
             (unsigned long)[nsWnd styleMask],
             (long)[nsWnd level],
             [nsWnd isVisible],
             [nsWnd isKeyWindow],
             [nsWnd isMainWindow]);
        DLOG("  collectionBehavior=0x%lx hasShadow=%d",
             (unsigned long)[nsWnd collectionBehavior],
             [nsWnd hasShadow]);
        DLOG("  backingScaleFactor=%g delegate=%p",
             [nsWnd backingScaleFactor],
             (__bridge void*)[nsWnd delegate]);

        NSView* view = [nsWnd contentView];
        if (!view)
        {
            DLOG("  contentView is nil");
            return;
        }
        DLOG("NSView ptr=%p class=%s",
             (__bridge void*)view,
             NSStringFromClass([view class]).UTF8String);
        const NSRect vFrame = [view frame];
        const NSRect vBounds = [view bounds];
        DLOG("  frame=(%g,%g %gx%g) bounds=(%g,%g %gx%g)",
             vFrame.origin.x, vFrame.origin.y, vFrame.size.width, vFrame.size.height,
             vBounds.origin.x, vBounds.origin.y, vBounds.size.width, vBounds.size.height);
        const NSSize backing = [view convertSizeToBacking:vBounds.size];
        DLOG("  backing size=%gx%g (ratio=%.2fx%.2f)",
             backing.width, backing.height,
             vBounds.size.width  > 0 ? backing.width  / vBounds.size.width  : -1.0,
             vBounds.size.height > 0 ? backing.height / vBounds.size.height : -1.0);
        DLOG("  wantsBestResolutionOpenGLSurface=%d wantsLayer=%d layer=%p",
             [view wantsBestResolutionOpenGLSurface],
             [view wantsLayer],
             (__bridge void*)[view layer]);
        if ([view layer])
        {
            CALayer* layer = [view layer];
            DLOG("  layer class=%s contentsScale=%g",
                 NSStringFromClass([layer class]).UTF8String,
                 [layer contentsScale]);
        }
    }
}

extern "C" void OpenXRay_NativeSDLInspect_Context(void* sdl_gl_context)
{
    DLOG("entry sdl_gl_context=%p", sdl_gl_context);
    if (!sdl_gl_context)
    {
        DLOG("context is null, abort");
        return;
    }

    @autoreleasepool
    {
        NSOpenGLContext* ctx = (__bridge NSOpenGLContext*)sdl_gl_context;
        DLOG("NSOpenGLContext ptr=%p class=%s",
             (__bridge void*)ctx,
             NSStringFromClass([ctx class]).UTF8String);

        NSView* view = [ctx view];
        DLOG("  [ctx view]=%p%s", (__bridge void*)view,
             view ? "" : " (nil)");

        DLOG("  currentVirtualScreen=%ld",
             (long)[ctx currentVirtualScreen]);
        DLOG("  isCurrent=%d (matches [NSOpenGLContext currentContext])",
             ([NSOpenGLContext currentContext] == ctx) ? 1 : 0);

        NSOpenGLPixelFormat* pf = [ctx pixelFormat];
        DLOG("  pixelFormat ptr=%p", (__bridge void*)pf);
        DumpPixelFormat(pf);

        GLint swapInterval = -1;
        [ctx getValues:&swapInterval forParameter:NSOpenGLCPSwapInterval];
        DLOG("  swapInterval=%d", (int)swapInterval);
    }
}
