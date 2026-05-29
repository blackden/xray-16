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

// Создать NSOpenGLPixelFormat (GL 4.1 Core / 24/8/24/8 / DoubleBuffer
// Accelerated NoRecovery) + NSOpenGLContext, прикрепить к contentView
// (он же `OpenXRay_NativeWindow_GetContentView`). На init: makeCurrent,
// дамп всех GL caps (vendor/renderer/version/GLSL), GL_DRAW_FRAMEBUFFER_BINDING,
// GL_VIEWPORT, glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER), drain GL errors.
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

#ifdef __cplusplus
} // extern "C"
#endif
