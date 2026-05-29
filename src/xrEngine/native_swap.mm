// Native swap реализация для A.7.4-restart step 4 (gitea #186).
//
// Компилируется БЕЗ PCH и БЕЗ unity (см. CMakeLists.txt) по той же
// причине что native_window.mm — AppKit BOOL clash.

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <cstdio>
#include <unistd.h>

#include "native_swap.h"

#define DLOG(fmt, ...) do {                                                         \
    char _buf[512];                                                                 \
    int _n = snprintf(_buf, sizeof _buf,                                            \
                      "==> a74p[native_swap:%s]: " fmt "\n",                        \
                      __func__, ##__VA_ARGS__);                                     \
    if (_n > 0)                                                                     \
        (void)::write(STDERR_FILENO, _buf, (size_t)_n);                             \
} while (0)

extern "C" void OpenXRay_NativeSwap_FlushBuffer(void* sdl_gl_context_void)
{
    static unsigned s_callIdx = 0;
    const bool verbose = (s_callIdx < 3);
    const bool pulse   = (s_callIdx > 0) && (s_callIdx % 600 == 0);

    if (!sdl_gl_context_void)
    {
        if (verbose)
            DLOG("call[%u] FAIL: sdl_gl_context is null — skipping flushBuffer", s_callIdx);
        ++s_callIdx;
        return;
    }

    NSOpenGLContext* ctx = (__bridge NSOpenGLContext*)sdl_gl_context_void;

    if (verbose)
        DLOG("call[%u] entry ctx=%p (NSOpenGLContext via SDL_GLContext cast)",
             s_callIdx, (__bridge void*)ctx);

    [ctx flushBuffer];

    if (verbose)
        DLOG("call[%u] exit (flushBuffer done)", s_callIdx);
    if (pulse)
        DLOG("call[%u] PULSE: still alive, ctx=%p", s_callIdx, (__bridge void*)ctx);

    ++s_callIdx;
}
