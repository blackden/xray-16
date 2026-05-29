// Step 1 diagnostic probe — A.7.4-restart (gitea #186).
//
// Один call который пробегает по `OpenXRay_NativeWindow_Create` +
// `OpenXRay_NativeGL_Create` + `Destroy` chain, без engine wiring.
// Цель: получить чистый log signal что macOS Cocoa + OpenGL.framework
// API delivers то, что мы ожидаем, ДО того как мы начнём подменять
// SDL пути на native в последующих шагах.
//
// Probe вызывается из `CRenderDevice::Initialize` ПОСЛЕ успешного
// `SDL_CreateWindow` — engine продолжает работать через SDL, probe —
// чисто параллельная диагностика. Если probe фейлит, engine этим не
// затрагивается; всё что нам надо — увидеть строчки в логе.
//
// После step 1 мы будем знать:
//   - создаётся ли NSWindow с нашими стилями
//   - создаётся ли NSOpenGLPixelFormat с GL 4.1 Core + 24/8/24/8 + MSAA off
//   - создаётся ли NSOpenGLContext, makeCurrent работает
//   - что glGetString возвращает (vendor/renderer/version/GLSL)
//   - GL_DRAW_FRAMEBUFFER_BINDING после setView+makeCurrent (=0?)
//   - glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) — COMPLETE? UNDEFINED?
//   - чистая ли GL error queue после init

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Запустить step 1 probe. Создаёт dormant native window + GL context,
// дампит caps, destroy'ит. Безопасно вызывать один раз из main thread.
void OpenXRay_NativeShellProbe(void);

#ifdef __cplusplus
} // extern "C"
#endif
