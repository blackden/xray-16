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

        const NSOpenGLPixelFormatAttribute attrs[] = {
            NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion4_1Core,
            NSOpenGLPFAColorSize,     24,
            NSOpenGLPFAAlphaSize,     8,
            NSOpenGLPFADepthSize,     24,
            NSOpenGLPFAStencilSize,   8,
            NSOpenGLPFADoubleBuffer,
            NSOpenGLPFAAccelerated,
            NSOpenGLPFANoRecovery,
            0
        };
        DLOG("alloc NSOpenGLPixelFormat: profile=Core4.1 color=24 alpha=8 depth=24 stencil=8 "
             "DoubleBuffer Accelerated NoRecovery");
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
