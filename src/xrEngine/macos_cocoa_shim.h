// macOS Cocoa shim — public C surface.
//
// Declarations consumed by the cross-platform engine TUs that need to hand off
// to Objective-C++ (.mm) code. Existing entries (OpenXRay_InstallCocoaShim,
// ArmLifecycleObservers, lifecycle setters) are intentionally NOT mirrored
// here yet — their callers use inline `extern "C"` declarations and we don't
// want to churn unrelated files. This header focuses on the A.3 NSEvent
// pipeline (issue #120): the queue record type + the C entries through which
// CInput interacts with the local-monitor handler.
//
// Implementation: src/xrEngine/macos_cocoa_shim.mm.

#pragma once

#ifdef __cplusplus
#include <cstdint>
#include <cstddef>
#else
#include <stdint.h>
#include <stddef.h>
#endif

enum OpenXRayNSEventKind
{
    OXR_NS_EVENT_KEY_DOWN      = 0,
    OXR_NS_EVENT_KEY_UP        = 1,
    OXR_NS_EVENT_FLAGS_CHANGED = 2,
    OXR_NS_EVENT_MOUSE_MOVE    = 3,
    OXR_NS_EVENT_MOUSE_DOWN    = 4,
    OXR_NS_EVENT_MOUSE_UP      = 5,
    OXR_NS_EVENT_MOUSE_DRAGGED = 6,
    OXR_NS_EVENT_SCROLL_WHEEL  = 7,
};

// Plain POD — copied through the ring queue. No constructors / ObjC types.
struct OpenXRayNSEventRecord
{
    int      kind;          // OpenXRayNSEventKind
    uint16_t keyCode;       // NSEvent.keyCode for key/flags events
    uint32_t modifierFlags; // event.modifierFlags & deviceIndependentFlagsMask
    uint8_t  mouseButton;   // 0=left, 1=right, 2=middle/other
    uint8_t  isARepeat;     // NSEvent.isARepeat (0 or 1)
    float    locX, locY;    // pixel coords (already flipped + scaled by backing factor)
    float    deltaX, deltaY;// mouse motion or scroll deltas
};

#ifdef __cplusplus
extern "C" {
#endif

// Установить NSEvent local monitor для keyboard / flagsChanged / mouse / scroll.
// Идемпотентно. Вызывается из OpenXRay_InstallCocoaShim() после Cmd+Q monitor'а.
// В этой стадии (commit 2a) handler работает в pass-through режиме — всегда
// возвращает event, ничего не consume'ит и в очередь не пишет.
void OpenXRay_InstallNSEventMonitor(void);

// Включить/выключить consume режим. При false handler возвращает event'ы
// дальше по цепочке (SDL обрабатывает как раньше). Default = true. В commit 2a
// этот flag только хранится — реальный switch в commit 2c.
void OpenXRay_SetNSEventInputEnabled(int enabled);

// Установить mouse capture mode. При captured=1 mouse-event records содержат
// deltas в deltaX/Y (locX/Y=0); при captured=0 — absolute pixel coords в
// locX/Y (deltas=0). Storage-only в commit 2a.
void OpenXRay_SetMouseCaptureMode(int captured);

// Дренировать очередь NSEvent'ов в массив `out` (capacity maxCount).
// Возвращает фактическое число записанных событий. В commit 2a очередь
// всегда пустая (handler stub), но API готов для drain'а из CInput в 2b/2c.
size_t OpenXRay_DrainNSEventQueue(struct OpenXRayNSEventRecord* out, size_t maxCount);

#ifdef __cplusplus
}
#endif
