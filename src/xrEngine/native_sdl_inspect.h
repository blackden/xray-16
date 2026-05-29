// SDL inspection probes — A.7.4b Step B.1 (gitea #188).
//
// Read-only диагностика: достаём NSWindow / NSView / NSOpenGLContext
// из SDL'овских handle'ов через SDL_GetWindowWMInfo / __bridge cast
// и дампим все важные свойства. Цель: точно знать что SDL делает с
// Cocoa'ой, чтобы в step B.2-B.3 наш native NSWindow + NSOpenGLContext
// мог воспроизвести точно тот же setup.
//
// Ничего не меняет — pure observability.

#pragma once

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

// Дамп SDL'овского NSWindow + contentView. Берёт NSWindow ptr через
// SDL_GetWindowWMInfo (cocoa.window) и логирует class / styleMask /
// frame / contentView / level / isVisible. Для contentView — class /
// frame / wantsBestResolutionOpenGLSurface / layer status.
void OpenXRay_NativeSDLInspect_Window(SDL_Window* sdlWnd);

// Дамп SDL'овского NSOpenGLContext. Принимает SDL_GLContext как void*
// (это и есть NSOpenGLContext* под капотом). Логирует [ctx view],
// pixelFormat attributes, currentVirtualScreen.
void OpenXRay_NativeSDLInspect_Context(void* sdl_gl_context);

// A.7.4b Step B.2 (gitea #188): достать NSView* contentView из SDL_Window
// без mutate state, для последующего setView: на нашем NSOpenGLContext.
// Возвращает NSView* как void* (caller __bridge cast'ает). nullptr при
// невалидном SDL_Window / не-Cocoa subsystem.
void* OpenXRay_NativeSDLInspect_GetContentView(SDL_Window* sdlWnd);

#ifdef __cplusplus
} // extern "C"
#endif
