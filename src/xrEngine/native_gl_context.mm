// Native NSOpenGLContext реализация для A.7.4-restart step 1 (gitea #186).
//
// Компилируется БЕЗ PCH и БЕЗ unity (см. CMakeLists.txt) по той же
// причине что `native_window.mm` — AppKit `BOOL=bool` vs xrCore
// `BOOL=int32_t` коллизия.
//
// NSOpenGLContext deprecated с macOS 10.14, но остаётся единственный
// путь получить настоящий GL 4.1 Core context без Metal shim. На фазе B
// мигрируем на CAMetalLayer; до тех пор живём с deprecation warnings.

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <OpenGL/gl3.h>
#include <cstdio>
#include <unistd.h>

#include "native_gl_context.h"

#define DLOG(fmt, ...) do {                                                         \
    char _buf[512];                                                                 \
    int _n = snprintf(_buf, sizeof _buf,                                            \
                      "==> a74p[native_gl:%s]: " fmt "\n",                          \
                      __func__, ##__VA_ARGS__);                                     \
    if (_n > 0)                                                                     \
        (void)::write(STDERR_FILENO, _buf, (size_t)_n);                             \
} while (0)

namespace
{
NSOpenGLContext*     g_ctx = nil;
NSOpenGLPixelFormat* g_pf  = nil;

const char* FbStatusName(GLenum s)
{
    switch (s)
    {
    case GL_FRAMEBUFFER_COMPLETE:                      return "COMPLETE";
    case GL_FRAMEBUFFER_UNDEFINED:                     return "UNDEFINED";
    case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:         return "INCOMPLETE_ATTACHMENT";
    case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: return "INCOMPLETE_MISSING_ATTACHMENT";
    case GL_FRAMEBUFFER_UNSUPPORTED:                   return "UNSUPPORTED";
    case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:        return "INCOMPLETE_MULTISAMPLE";
    default:                                           return "?";
    }
}

const char* GlErrorName(GLenum e)
{
    switch (e)
    {
    case GL_NO_ERROR:                      return "GL_NO_ERROR";
    case GL_INVALID_ENUM:                  return "GL_INVALID_ENUM";
    case GL_INVALID_VALUE:                 return "GL_INVALID_VALUE";
    case GL_INVALID_OPERATION:             return "GL_INVALID_OPERATION";
    case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
    case GL_OUT_OF_MEMORY:                 return "GL_OUT_OF_MEMORY";
    default:                               return "?";
    }
}
} // namespace

extern "C" bool OpenXRay_NativeGL_Create(void* contentViewVoid)
{
    DLOG("entry contentView=%p", contentViewVoid);

    if (g_ctx)
    {
        DLOG("already created (ctx=%p), idempotent return true", (__bridge void*)g_ctx);
        return true;
    }
    if (!contentViewVoid)
    {
        DLOG("FAIL: contentView is null");
        return false;
    }

    @autoreleasepool
    {
        NSView* view = (__bridge NSView*)contentViewVoid;

        // SDL-matching attributes (A.7.4b step B.1 inspect dump):
        // SDL ставит Color=32 (включая alpha bits в общий count) Depth=32
        // (больше precision для дальних объектов) и НЕ ставит NoRecovery
        // (разрешает software fallback). Воспроизводим тот же setup чтобы
        // render pipeline не получил меньше precision'а при переходе.
        const NSOpenGLPixelFormatAttribute attrs[] = {
            NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion4_1Core,
            NSOpenGLPFAColorSize,     32,
            NSOpenGLPFAAlphaSize,     8,
            NSOpenGLPFADepthSize,     32,
            NSOpenGLPFAStencilSize,   8,
            NSOpenGLPFADoubleBuffer,
            NSOpenGLPFAAccelerated,
            0
        };
        DLOG("alloc NSOpenGLPixelFormat: profile=Core4.1 color=32 alpha=8 depth=32 stencil=8 "
             "DoubleBuffer Accelerated (SDL-matching, A.7.4b)");
        g_pf = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
        if (!g_pf)
        {
            DLOG("FAIL: NSOpenGLPixelFormat returned nil — GPU/driver не поддерживает запрошенные атрибуты");
            return false;
        }
        DLOG("NSOpenGLPixelFormat OK ptr=%p", (__bridge void*)g_pf);

        DLOG("alloc NSOpenGLContext shareContext=nil");
        g_ctx = [[NSOpenGLContext alloc] initWithFormat:g_pf shareContext:nil];
        if (!g_ctx)
        {
            DLOG("FAIL: NSOpenGLContext alloc returned nil");
            g_pf = nil;
            return false;
        }
        DLOG("NSOpenGLContext OK ptr=%p", (__bridge void*)g_ctx);

        DLOG("setView contentView=%p", contentViewVoid);
        [g_ctx setView:view];

        DLOG("makeCurrentContext");
        [g_ctx makeCurrentContext];

        // ----- GL capabilities dump -----
        const GLubyte* vendor   = glGetString(GL_VENDOR);
        const GLubyte* renderer = glGetString(GL_RENDERER);
        const GLubyte* version  = glGetString(GL_VERSION);
        const GLubyte* glsl     = glGetString(GL_SHADING_LANGUAGE_VERSION);
        DLOG("GL_VENDOR='%s'",                   vendor   ? (const char*)vendor   : "(null)");
        DLOG("GL_RENDERER='%s'",                 renderer ? (const char*)renderer : "(null)");
        DLOG("GL_VERSION='%s'",                  version  ? (const char*)version  : "(null)");
        DLOG("GL_SHADING_LANGUAGE_VERSION='%s'", glsl     ? (const char*)glsl     : "(null)");

        // ----- FBO 0 status -----
        GLint fbBinding = -1;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbBinding);
        DLOG("GL_DRAW_FRAMEBUFFER_BINDING=%d (ожидается 0=default framebuffer)", (int)fbBinding);

        GLint readBinding = -1;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readBinding);
        DLOG("GL_READ_FRAMEBUFFER_BINDING=%d", (int)readBinding);

        GLint viewport[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_VIEWPORT, viewport);
        DLOG("GL_VIEWPORT=%d,%d,%d,%d", viewport[0], viewport[1], viewport[2], viewport[3]);

        const GLenum fbStatus = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
        DLOG("glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)=0x%x (%s)",
             (unsigned)fbStatus, FbStatusName(fbStatus));

        // ----- Drain GL errors -----
        bool clean = false;
        for (int i = 0; i < 10; ++i)
        {
            const GLenum err = glGetError();
            if (err == GL_NO_ERROR)
            {
                DLOG("glGetError loop[%d]=GL_NO_ERROR (clean)", i);
                clean = true;
                break;
            }
            DLOG("glGetError loop[%d]=0x%x (%s)", i, (unsigned)err, GlErrorName(err));
        }
        if (!clean)
            DLOG("WARNING: glGetError loop hit 10-iteration cap without GL_NO_ERROR");
    }
    return true;
}

extern "C" void OpenXRay_NativeGL_Destroy(void)
{
    DLOG("entry");
    if (!g_ctx)
    {
        DLOG("no context, return");
        return;
    }

    @autoreleasepool
    {
        DLOG("clearCurrentContext + clearDrawable + release ctx=%p", (__bridge void*)g_ctx);
        [NSOpenGLContext clearCurrentContext];
        [g_ctx clearDrawable];
        g_ctx = nil;
        g_pf  = nil;
    }
    DLOG("done");
}

extern "C" void OpenXRay_NativeGL_MakeCurrent(void)
{
    if (!g_ctx)
    {
        DLOG("called before Create — noop");
        return;
    }
    [g_ctx makeCurrentContext];
}

extern "C" void OpenXRay_NativeGL_ClearCurrent(void)
{
    [NSOpenGLContext clearCurrentContext];
}

extern "C" void* OpenXRay_NativeGL_GetNSContext(void)
{
    return (__bridge void*)g_ctx;
}

// ---------------------------------------------------------------------------
// A.7.4b Step B.2 (gitea #188): persistent variant.
//
// Не использует g_ctx / g_pf globals — создаёт fresh NSOpenGLContext +
// NSOpenGLPixelFormat с CFRetain'ом (через __bridge_retained), возвращает
// caller'у NSOpenGLContext* как void*. Caller потом передаёт обратно в
// DestroyPersistent для release.
//
// Используется в glHW.cpp как замена SDL_GL_CreateContext под env var
// OPENXRAY_NATIVE_GL=1. SDL_GL_MakeCurrent / SwapWindow / DeleteContext
// на macOS — обёртки над [NSOpenGLContext ...], поэтому наш контекст
// совместим с SDL API.
// ---------------------------------------------------------------------------

extern "C" void* OpenXRay_NativeGL_CreatePersistent(void* contentViewVoid)
{
    DLOG("[persistent] entry contentView=%p", contentViewVoid);

    if (!contentViewVoid)
    {
        DLOG("[persistent] FAIL: contentView is null");
        return nullptr;
    }

    NSOpenGLContext* ctx = nil;

    @autoreleasepool
    {
        NSView* view = (__bridge NSView*)contentViewVoid;

        // SDL-matching attributes (см. step B.1 inspect dump).
        const NSOpenGLPixelFormatAttribute attrs[] = {
            NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion4_1Core,
            NSOpenGLPFAColorSize,     32,
            NSOpenGLPFAAlphaSize,     8,
            NSOpenGLPFADepthSize,     32,
            NSOpenGLPFAStencilSize,   8,
            NSOpenGLPFADoubleBuffer,
            NSOpenGLPFAAccelerated,
            0
        };
        DLOG("[persistent] alloc NSOpenGLPixelFormat: profile=Core4.1 color=32 alpha=8 depth=32 stencil=8");
        NSOpenGLPixelFormat* pf = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
        if (!pf)
        {
            DLOG("[persistent] FAIL: NSOpenGLPixelFormat returned nil");
            return nullptr;
        }
        DLOG("[persistent] NSOpenGLPixelFormat OK ptr=%p", (__bridge void*)pf);

        DLOG("[persistent] alloc NSOpenGLContext shareContext=nil");
        ctx = [[NSOpenGLContext alloc] initWithFormat:pf shareContext:nil];
        if (!ctx)
        {
            DLOG("[persistent] FAIL: NSOpenGLContext alloc returned nil");
            return nullptr;
        }
        DLOG("[persistent] NSOpenGLContext OK ptr=%p", (__bridge void*)ctx);

        DLOG("[persistent] setView contentView=%p", contentViewVoid);
        [ctx setView:view];

        DLOG("[persistent] makeCurrentContext");
        [ctx makeCurrentContext];

        // ----- GL capabilities dump (caller'у полезно знать что у него) -----
        const GLubyte* vendor   = glGetString(GL_VENDOR);
        const GLubyte* renderer = glGetString(GL_RENDERER);
        const GLubyte* version  = glGetString(GL_VERSION);
        const GLubyte* glsl     = glGetString(GL_SHADING_LANGUAGE_VERSION);
        DLOG("[persistent] GL_VENDOR='%s'",                   vendor   ? (const char*)vendor   : "(null)");
        DLOG("[persistent] GL_RENDERER='%s'",                 renderer ? (const char*)renderer : "(null)");
        DLOG("[persistent] GL_VERSION='%s'",                  version  ? (const char*)version  : "(null)");
        DLOG("[persistent] GL_SHADING_LANGUAGE_VERSION='%s'", glsl     ? (const char*)glsl     : "(null)");

        GLint fbBinding = -1;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbBinding);
        DLOG("[persistent] GL_DRAW_FRAMEBUFFER_BINDING=%d", (int)fbBinding);

        const GLenum fbStatus = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
        DLOG("[persistent] glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)=0x%x (%s)",
             (unsigned)fbStatus, FbStatusName(fbStatus));

        bool clean = false;
        for (int i = 0; i < 10; ++i)
        {
            const GLenum err = glGetError();
            if (err == GL_NO_ERROR)
            {
                DLOG("[persistent] glGetError loop[%d]=GL_NO_ERROR (clean)", i);
                clean = true;
                break;
            }
            DLOG("[persistent] glGetError loop[%d]=0x%x (%s)", i, (unsigned)err, GlErrorName(err));
        }
        if (!clean)
            DLOG("[persistent] WARNING: glGetError loop hit 10-iteration cap without GL_NO_ERROR");
    }

    // __bridge_retained даёт +1 retain count на ctx и возвращает void*
    // который caller должен передать обратно в DestroyPersistent
    // (__bridge_transfer там вернёт retain count в ARC, alloc'нув от
    // void* обратно в strong NSOpenGLContext* и автоматически отпустит).
    void* result = (__bridge_retained void*)ctx;
    DLOG("[persistent] done — returning void* %p (retain transferred to caller)", result);
    return result;
}

extern "C" void OpenXRay_NativeGL_DestroyPersistent(void* nsContextVoid)
{
    DLOG("[persistent] destroy entry ctx=%p", nsContextVoid);
    if (!nsContextVoid)
    {
        DLOG("[persistent] destroy: null, return");
        return;
    }

    @autoreleasepool
    {
        // __bridge_transfer: void* возвращается в ARC ownership,
        // переменная ctx будет release'нута на выходе из autoreleasepool.
        NSOpenGLContext* ctx = (__bridge_transfer NSOpenGLContext*)nsContextVoid;
        DLOG("[persistent] clearCurrentContext + clearDrawable");
        if ([NSOpenGLContext currentContext] == ctx)
            [NSOpenGLContext clearCurrentContext];
        [ctx clearDrawable];
        // ctx будет release'нут когда autoreleasepool drain'ится
        (void)ctx;
    }
    DLOG("[persistent] destroy done");
}
