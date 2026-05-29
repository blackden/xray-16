// Native swap surface — A.7.4-restart Step 4 (gitea #186).
//
// Минимальный C-API чтобы заменить SDL_GL_SwapWindow на
// [NSOpenGLContext flushBuffer], не трогая всё остальное в engine.
//
// SDL2 на macOS создаёт настоящий NSOpenGLContext под капотом —
// `SDL_GLContext` (=void*) фактически указатель на NSOpenGLContext.
// Берём его как есть и вызываем flushBuffer вместо SwapWindow.
//
// Это первый native call в render path: SDL остаётся owner'ом окна и
// контекста, мы только меняем презентацию backbuffer'а.
//
// Включается env var OPENXRAY_NATIVE_SWAP=1. Default OFF — никакой
// регрессии, SDL_GL_SwapWindow продолжает работать.
//
// Если step 4 работает чисто — step 5 будет переход на собственный
// NSOpenGLContext (созданный через native_gl_context.h), не SDL'овский.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Презентировать backbuffer SDL'овского NSOpenGLContext через
// [NSOpenGLContext flushBuffer]. `sdl_gl_context_void` — это
// SDL_GL_GetCurrentContext() или CHW::m_context, brigded cast'нется
// внутри к NSOpenGLContext*.
//
// На первых N вызовах + periodic pulse логируется через
// ==> a74p[native_swap:...]:.
void OpenXRay_NativeSwap_FlushBuffer(void* sdl_gl_context_void);

#ifdef __cplusplus
} // extern "C"
#endif
