// Step 1 diagnostic probe — реализация (A.7.4-restart, gitea #186).
//
// Компилируется БЕЗ PCH и БЕЗ unity (см. CMakeLists.txt) — этот TU не
// использует AppKit напрямую, но включается рядом с native_window.h /
// native_gl_context.h и хочет тот же diagnostic logging формат.

#include <cstdio>
#include <unistd.h>

#include "native_shell_probe.h"
#include "native_window.h"
#include "native_gl_context.h"

#define DLOG(fmt, ...) do {                                                         \
    char _buf[512];                                                                 \
    int _n = snprintf(_buf, sizeof _buf,                                            \
                      "==> a74p[shell_probe:%s]: " fmt "\n",                        \
                      __func__, ##__VA_ARGS__);                                     \
    if (_n > 0)                                                                     \
        (void)::write(STDERR_FILENO, _buf, (size_t)_n);                             \
} while (0)

extern "C" void OpenXRay_NativeShellProbe(void)
{
    DLOG("=== A.7.4-restart Step 1 probe BEGIN ===");

    if (!OpenXRay_NativeWindow_Create(640, 480, "OpenXRay native shell probe"))
    {
        DLOG("FAIL: native window create returned false, abort probe");
        DLOG("=== probe END (early) ===");
        return;
    }

    int bw = 0, bh = 0;
    OpenXRay_NativeWindow_GetBackingSize(&bw, &bh);
    DLOG("post-window backing size = %dx%d", bw, bh);

    void* nsWindow    = OpenXRay_NativeWindow_GetNSWindow();
    void* contentView = OpenXRay_NativeWindow_GetContentView();
    DLOG("NSWindow ptr=%p  contentView ptr=%p", nsWindow, contentView);

    if (!OpenXRay_NativeGL_Create(contentView))
    {
        DLOG("FAIL: native GL context create returned false");
        OpenXRay_NativeWindow_Destroy();
        DLOG("=== probe END (early) ===");
        return;
    }

    OpenXRay_NativeGL_ClearCurrent();
    OpenXRay_NativeGL_Destroy();
    OpenXRay_NativeWindow_Destroy();

    DLOG("=== A.7.4-restart Step 1 probe END (clean) ===");
}
