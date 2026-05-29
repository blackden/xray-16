// Native NSWindow surface — A.7.4-restart Step 1 (gitea #186).
//
// Минимальный публичный C-API для создания / уничтожения NSWindow
// и получения NSView contentView, к которому позже подцепится
// NSOpenGLContext. На Step 1 окно создаётся как dormant probe — не
// показывается пользователю (не вызывается makeKeyAndOrderFront).
//
// Все Cocoa-операции внутри пишут диагностику в stderr (попадает в
// `~/Library/Logs/OpenXRay/openxray.log` через launcher) с префиксом
// `==> a74p[native_window:...]:`. После step 1 у нас будет жёсткий
// сигнал «macOS API delivers что мы ждём» или «вот ровно тут ломается».
//
// Заголовок без AppKit-зависимостей — включается из обычных C++ TU.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Создать NSWindow + NSView contentView. На Step 1 окно НЕ показывается
// (без makeKeyAndOrderFront) — это dormant probe для проверки что
// Cocoa-цепочка отрабатывает. Возвращает true при успехе.
// Идемпотентно: повторный вызов = no-op + true.
bool OpenXRay_NativeWindow_Create(int width, int height, const char* title);

// Destroy + release. Идемпотентно.
void OpenXRay_NativeWindow_Destroy(void);

// Возвращает NSWindow* как void*. Для Apple side cast обратно. nullptr
// если ничего не создано.
void* OpenXRay_NativeWindow_GetNSWindow(void);

// Возвращает NSView* contentView как void*. Это поверхность, к которой
// позже NSOpenGLContext будет setView:. nullptr если ничего не создано.
void* OpenXRay_NativeWindow_GetContentView(void);

// HiDPI authority: backing-pixel размеры contentView через
// `[view convertSizeToBacking:[view bounds].size]`. Для HiDPI-математики
// engine должен идти ТОЛЬКО через эту функцию, не считать сам scale * logical.
void OpenXRay_NativeWindow_GetBackingSize(int* out_w, int* out_h);

// A.7.4c Step C.1 (gitea #190): показать окно через makeKeyAndOrderFront:.
// На step 1 (#186) probe Create намеренно НЕ вызывал makeKeyAndOrderFront —
// окно было dormant probe. Для C.1 нам нужно visible окно alongside SDL'овского.
void OpenXRay_NativeWindow_Show(void);

// A.7.4c Step C.1: установить collection behavior. Принимает unsigned long
// (NSWindowCollectionBehavior), бит-флаги совпадают с Apple'овскими
// определениями (например 0x80 = NSWindowCollectionBehaviorFullScreenPrimary,
// что SDL ставит автоматически при SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES=1).
// Биты OR'ятся к существующему behavior'у, не replaces.
void OpenXRay_NativeWindow_SetCollectionBehavior(unsigned long behavior);

// A.7.4c Step C.1: установить title окна. UTF-8 строка. Безопасно вызывать
// после Create.
void OpenXRay_NativeWindow_SetTitle(const char* utf8Title);

#ifdef __cplusplus
} // extern "C"
#endif
