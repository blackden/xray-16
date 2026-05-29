// Native NSOpenGLContext surface — A.7.4-restart Step 1 (gitea #186).
//
// Минимальный C-API для создания / destroy NSOpenGLPixelFormat +
// NSOpenGLContext, с дампом всех GL capabilities при init. На Step 1
// контекст не используется engine'ом — это dormant probe для проверки
// что GL 4.1 Core реально доступен через NSOpenGLContext, что
// driver/GPU не отбрасывает наш pixel format chain, и какие именно
// `glGetString` strings vs FBO 0 status мы получаем.
//
// На последующих шагах этот же API будет дёрнуть glHW.cpp вместо
// `SDL_GL_CreateContext` — поэтому surface уже минимально-достаточный.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Создать NSOpenGLPixelFormat (GL 4.1 Core / Color=32 Alpha=8 Depth=32
// Stencil=8 / DoubleBuffer Accelerated) + NSOpenGLContext, прикрепить
// к contentView (он же `OpenXRay_NativeWindow_GetContentView`). На init:
// makeCurrent, дамп всех GL caps (vendor/renderer/version/GLSL),
// GL_DRAW_FRAMEBUFFER_BINDING, GL_VIEWPORT, glCheckFramebufferStatus
// (GL_DRAW_FRAMEBUFFER), drain GL errors.
//
// Атрибуты SDL-matching (см. native_sdl_inspect dump из A.7.4b step B.1):
// SDL ставит Color=32 Depth=32 без NoRecovery — мы повторяем чтобы render
// pipeline получил тот же precision-budget.
//
// Возвращает true при успехе. Идемпотентно.
bool OpenXRay_NativeGL_Create(void* contentView);

// Destroy + release. Идемпотентно.
void OpenXRay_NativeGL_Destroy(void);

// makeCurrentContext / clearCurrentContext. На Step 1 движок их не
// использует, но они нужны для самого probe'а.
void OpenXRay_NativeGL_MakeCurrent(void);
void OpenXRay_NativeGL_ClearCurrent(void);

// Возвращает NSOpenGLContext* как void*. nullptr если не создан.
void* OpenXRay_NativeGL_GetNSContext(void);

// A.7.4b Step B.2 (gitea #188): создать persistent NSOpenGLContext для
// использования engine'ом. В отличие от Create + Destroy probe pair'а
// контекст retain'ится и возвращается caller'у в виде void* (cast'нется
// обратно в SDL_GLContext на стороне glHW.cpp). Engine использует
// SDL_GL_MakeCurrent / SwapWindow / DeleteContext — на macOS SDL2 их
// implementации это просто обёртки над [NSOpenGLContext ...], поэтому
// наш контекст совместим с SDL API без дополнительных адаптеров.
//
// Возвращает NSOpenGLContext* как void* при успехе. nullptr при failure.
// Логирует pixel format choices + caps как обычный Create.
void* OpenXRay_NativeGL_CreatePersistent(void* contentView);

// A.7.4b Step B.2: destroy persistent context. Зеркало для CreatePersistent.
void OpenXRay_NativeGL_DestroyPersistent(void* nsContextVoid);

// A.7.4b Step B.2: makeCurrent на произвольный NSOpenGLContext (не только
// global g_ctx). Используется в glHW.cpp::MakeContextCurrent под native
// path — SDL_GL_MakeCurrent на macOS внутренне проверяет SDL'овский
// context tracking и отвергает наш ctx, поэтому обходим SDL.
//
// Возвращает true если successfully сделали makeCurrent (или ctx был nil
// и clearCurrent сделан).
bool OpenXRay_NativeGL_MakeCurrentArg(void* nsContextVoid);

#ifdef __cplusplus
} // extern "C"
#endif
