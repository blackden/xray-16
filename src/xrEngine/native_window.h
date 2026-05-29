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

// ---------------------------------------------------------------------------
// A.7.4 C.4a (gitea #192): расширенный C-API. Под OPENXRAY_NATIVE_WINDOW=1
// SDL_CreateWindow на Apple skip'ается, и engine-consumer'ы маршрутизируются
// через эти helpers. Все null-safe (no-op если g_window нет).
// ---------------------------------------------------------------------------

// Установить минимальный размер contentView в points: `[g_window setContentMinSize:]`.
void OpenXRay_NativeWindow_SetMinimumSize(int w, int h);

// `[contentView bounds]` (origin всегда 0,0). Размеры в points.
void OpenXRay_NativeWindow_GetClientRect(int* x, int* y, int* w, int* h);

// `[window frame]` — позиция окна в screen-coordinates + frame size (с titlebar)
// в points.
void OpenXRay_NativeWindow_GetFrameRect(int* x, int* y, int* w, int* h);

// `[window isKeyWindow]` — есть ли у нашего окна focus.
bool OpenXRay_NativeWindow_IsKeyWindow(void);

// `[window isMiniaturized]` — свёрнуто ли окно в Dock.
bool OpenXRay_NativeWindow_IsMinimized(void);

// Warp cursor в координаты point'ы относительно contentView. Конвертирует в
// screen-global через `[window convertRectToScreen:]` и зовёт CGWarpMouseCursorPosition.
void OpenXRay_NativeWindow_WarpCursorInWindow(int x, int y);

// CGAssociateMouseAndMouseCursorPosition(associated ? 1 : 0). `associated=false`
// untether'ит cursor для relative mouse-look (analog SDL_SetWindowGrab(true) +
// SDL_SetRelativeMouseMode(true) на macOS).
void OpenXRay_NativeWindow_SetCursorAssociated(bool associated);

// ---------------------------------------------------------------------------
// A.7.4 C.4a: per-frame event aggregator. NSWindowDelegate (windowDidResize:,
// windowWillClose:, windowDidBecomeKey:, windowDidMiniaturize: и co.) НЕ
// зовёт engine синхронно — события enqueue'ятся в last-wins slot per type,
// drain'ятся через PollEvents из engine main loop (OpenXRay_RunPerFrameMacOSHooks).
//
// Все callback'и опциональны (nullptr = skip). Drain'ится at most один event
// каждого типа за вызов: resize + move сворачиваются в один RESIZE callback
// с текущим backing size; activate/minimize → последнее значение.
// ---------------------------------------------------------------------------
void OpenXRay_NativeWindow_PollEvents(
    void (*on_resize)(int w, int h),
    void (*on_close)(void),
    void (*on_activate)(bool active),
    void (*on_minimize)(bool minimized));

// Признак того, что engine работает на native render path (NATIVE_WINDOW=1).
// Установлен Device_Initialize при создании native окна и читается
// renderer'ом (glHW.cpp) чтобы пропустить SDL-only operations.
bool OpenXRay_IsNativeWindowRender(void);
void OpenXRay_SetNativeWindowRender(bool enabled);

#ifdef __cplusplus
} // extern "C"
#endif
