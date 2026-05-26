# Design: A.3 NSEvent input pipeline

Дата: 2026-05-26
Эпик: [#87](https://git.fedorov.tech/ragnar/xray-16/issues/87) (native macOS shell)
Roadmap: [`2026-05-25-native-shell-roadmap.md`](2026-05-25-native-shell-roadmap.md) §3 шаг A.3
Brainstorm: эта сессия (2026-05-26), фолоу-ап после A.2 merge (PR #118)

## 1. Контекст

A.1 (Cocoa lifecycle observers, PR #115) и A.2 (ghttp dispatch_queue worker, PR
#118) merged. Главный цикл движка всё ещё SDL pump
(`CApplication::Run` в `src/xrEngine/x_ray.cpp:425-522`); A.2 в реальности
закрыл только ghttp polling, оригинальный roadmap-scope «NSRunLoop как
single source of truth» отложен. Этот шаг — следующая отрезанная рука у
SDL: клавиатура, мышь, скролл, modifier flags идут через NSEvent на
macOS, transit через ту же `IInputReceiver` callback-цепочку.

**Цель**: после A.3 SDL **не дёргается** для keyboard/mouse/scroll
event'ов; controller (gamepad) и window events продолжают идти через
SDL — это убирается в следующих шагах (A.6 зачистка, A.7 SDL out).

**Гейт «A.3 done»** (по roadmap'у §3 + конкретизация в этом дизайне):

- Все input scenarios CoP работают (keyboard+mouse — controller остаётся через SDL)
- Layout switching eng↔rus не теряет ввод
- Mouse capture/release корректный
- Stuck-key recovery работает (focus loss, sleep, cvar flip)
- 30-минутный CoP soak на M1 без регрессий

## 2. Архитектура

### Поток данных в одном кадре

```
main loop frame N (CApplication::Run, x_ray.cpp:425-522):
  ├─ SDL_PeepEvents(SDL_WINDOWEVENT)            [x_ray.cpp:431-478]
  │   └─ SDL внутри вызывает [NSApp nextEvent]
  │       └─ AppKit sendEvent: → local monitor handler (OpenXRayCocoaShim)
  │           ├─ Cmd+Q ?           → consume + OpenXRay_RequestGracefulQuit() (как сегодня)
  │           ├─ nsevent_input==0 ? → return event (rollback path — SDL обрабатывает)
  │           ├─ keyboard / mouse / scroll ?
  │           │   → push в g_nsEventQueue + return nil (consumed)
  │           └─ остальное (window, controller, ...) → return event (SDL)
  └─ Device.ProcessFrame()                       [Device.cpp:280]
      └─ seqFrame.Process()
          └─ CInput::OnFrame()                   [xr_input.cpp:775]
              ├─ #if XR_PLATFORM_APPLE: NSEventDrain()          ← новый
              │   └─ для каждого record:
              │       ├─ KeyDown/KeyUp     → IR_OnKeyboardPress/Release
              │       ├─ FlagsChanged diff → ↑ для модификаторов
              │       └─ Mouse*/ScrollWheel → IR_OnMouse*/IR_OnMouseWheel
              ├─ KeyUpdate()      [SDL_PeepEvents для KEYDOWN/UP — Apple-gate early-return]
              ├─ MouseUpdate()    [то же для MOUSE*]
              └─ ControllerUpdate() [продолжает через SDL]
```

Local monitor handler и `NSEventDrain` оба исполняются на main thread —
никаких atomics или mutex'ов не нужно. Producer/consumer на одном потоке,
разнесённый во времени в пределах одного кадра.

### Что отсекается от SDL

После A.3 SDL **не получает** на macOS:
- `SDL_KEYDOWN`, `SDL_KEYUP`, `SDL_TEXTINPUT`, `SDL_KEYMAPCHANGED`
- `SDL_MOUSEMOTION`, `SDL_MOUSEBUTTONDOWN`, `SDL_MOUSEBUTTONUP`, `SDL_MOUSEWHEEL`

SDL продолжает обрабатывать:
- `SDL_WINDOWEVENT_*` (focus, resize, expose) — A.6 уровень
- `SDL_CONTROLLER*` (gamepad) — отдельный шаг позже
- `SDL_QUIT` — graceful quit маршрут не трогаем

### Rollback

Cvar `nsevent_input` (default `1`). Зарегистрирован только под
`#if XR_PLATFORM_APPLE` в `xrRender_console.cpp` (на других платформах
cvar отсутствует — нечего откатывать).

При `nsevent_input 0`:
- Local monitor возвращает event'ы (не consume) для input типов — SDL
  начинает их получать как раньше
- `NSEventDrain()` no-op
- SDL KeyUpdate/MouseUpdate Apple-gate early-return снимается → SDL
  pipeline активен (был ранее no-op потому что NSEvent consume'ил)

Flip cvar'а в рантайме (любое направление) триггерит
`OpenXRay_SyntheticReleaseAllKeys()` — `CInput` итерируется по своему
`keyboardState`, для каждой зажатой клавиши вызывает
`cbStack.back()->IR_OnKeyboardRelease(scancode)` и обнуляет state.
Это soft-reset, избегает stuck-key между двумя pipeline'ами.

## 3. Компоненты

### 3.1. `OpenXRayCocoaShim` — расширение local monitor

Файл: `src/xrEngine/macos_cocoa_shim.mm`.

Текущий монитор (`macos_cocoa_shim.mm:208-223`) ловит только
`NSEventMaskKeyDown` для Cmd+Q. Расширяем маску:

```objc
static const NSEventMask kInputMask =
    NSEventMaskKeyDown | NSEventMaskKeyUp | NSEventMaskFlagsChanged |
    NSEventMaskMouseMoved |
    NSEventMaskLeftMouseDown  | NSEventMaskLeftMouseUp  | NSEventMaskLeftMouseDragged  |
    NSEventMaskRightMouseDown | NSEventMaskRightMouseUp | NSEventMaskRightMouseDragged |
    NSEventMaskOtherMouseDown | NSEventMaskOtherMouseUp | NSEventMaskOtherMouseDragged |
    NSEventMaskScrollWheel;
```

Handler logic (приоритет сверху вниз):
1. Cmd+Q (keyCode == `kVK_ANSI_Q` && Cmd held) → `OpenXRay_RequestGracefulQuit()` + return nil
2. `g_nsEventInputEnabled == false` → return event (rollback)
3. Иначе — push в `g_nsEventQueue`, return nil

### 3.2. Очередь — фиксированный ring `[256]`

POD-копия полей в момент push'а (не taскаем NSEvent retain через C++ границу):

```cpp
// src/xrEngine/macos_cocoa_shim.h
enum class OpenXRayNSEventKind : u8 {
    KeyDown, KeyUp, FlagsChanged,
    MouseMove, MouseDown, MouseUp, MouseDragged,
    ScrollWheel,
};

struct OpenXRayNSEventRecord {
    OpenXRayNSEventKind kind;
    u16   keyCode;        // NSEvent keyCode для key events
    u32   modifierFlags;  // event.modifierFlags & deviceIndependentFlagsMask
    u8    mouseButton;    // 0=left, 1=right, 2=middle/other
    bool  isARepeat;      // NSEvent.isARepeat
    float locX, locY;     // в pixel coords (после flip Y и backingScaleFactor)
    float deltaX, deltaY; // mouse motion / scroll deltas
};
```

Переполнение: drop oldest + `Msg("! NS event queue overflow")` через
one-shot `static bool` (стандартный pattern движка).

### 3.3. C entry points

В `src/xrEngine/macos_cocoa_shim.h`:

```cpp
void OpenXRay_InstallNSEventMonitor();
void OpenXRay_SetNSEventInputEnabled(bool enabled);
size_t OpenXRay_DrainNSEventQueue(OpenXRayNSEventRecord* out, size_t maxCount);
void OpenXRay_SyntheticReleaseAllKeys();  // для focus-loss / cvar flip
```

`OpenXRay_InstallNSEventMonitor()` вызывается из существующего
`OpenXRay_InstallCocoaShim()` (`macos_cocoa_shim.mm:184-237`).

### 3.4. NSEvent keyCode → SDL_Scancode таблица

Статический `constexpr SDL_Scancode kNSKeyCodeToSDLScancode[128]` в
`src/xrEngine/xr_input.cpp` под `#if XR_PLATFORM_APPLE`. Заполняется по
`<Carbon/HIToolbox/Events.h>` (`kVK_ANSI_A` etc). Это репликация того
самого маппинга что SDL делает внутри в `SDL_cocoakeyboard.m`.

Dev-only verify под `dev_tools 1` cvar: команда `verify_input_table` —
одноразово проходит все keys из default bindings
(`xr_level_controller.cpp:225`), сверяет наш table с
`SDL_GetScancodeFromKey` — diff'ы логирует.

### 3.5. FlagsChanged → modifier press/release

В shim'е держим `static uint32_t g_lastModifierFlags`. При каждом
`NSEventTypeFlagsChanged`:

```
diff = event.modifierFlags XOR g_lastModifierFlags
для каждого изменившегося flag-бита:
  если бит появился → push KeyDown(scancode_для_этого_modifier)
  если бит исчез   → push KeyUp(...)
g_lastModifierFlags = event.modifierFlags
```

Маппинг (NSEvent keyCode → SDL_Scancode):
- `kVK_Shift` (0x38) → `SDL_SCANCODE_LSHIFT`
- `kVK_RightShift` (0x3C) → `SDL_SCANCODE_RSHIFT`
- `kVK_Control` (0x3B) → `SDL_SCANCODE_LCTRL`
- `kVK_RightControl` (0x3E) → `SDL_SCANCODE_RCTRL`
- `kVK_Option` (0x3A) → `SDL_SCANCODE_LALT`
- `kVK_RightOption` (0x3D) → `SDL_SCANCODE_RALT`
- `kVK_Command` (0x37) → `SDL_SCANCODE_LGUI`
- `kVK_RightCommand` (0x36) → `SDL_SCANCODE_RGUI`
- `kVK_CapsLock` (0x39) → `SDL_SCANCODE_CAPSLOCK` (тап — Down+Up подряд)

### 3.6. Mouse motion — два режима

Текущий код использует `SDL_SetRelativeMouseMode` для captured-режима.
Shim знает о состоянии через `OpenXRay_SetMouseCaptureMode(bool)`
(новый entry; `CInput` уведомляет shim при capture/release).

- **Captured**: `event.deltaX/deltaY` → `IR_OnMouseMove(int(dx), int(dy))`, locX=locY=0
- **Не captured**: `event.locationInWindow` → flip Y + умножение на
  `backingScaleFactor` → absolute pixel coords → `IR_OnMouseMove(x, y)`

Конверсия Y: `y_pixel = (window.contentLayoutRect.size.height - cocoaY) * backingScaleFactor`.
`backingScaleFactor` кэшируется в shim'е, обновляется по
`windowDidChangeBackingProperties:` (добавляем в `OpenXRayCocoaShim`).

### 3.7. Scroll

`event.scrollingDeltaX/scrollingDeltaY` → `IR_OnMouseWheel(int(dx), int(dy))`.
`hasPreciseScrollingDeltas` мы не различаем — округляем к int.
Trackpad pinch/swipe — не обрабатываем (out of scope).

## 4. Data flow примеры

### 4.1. «W» нажата (US раскладка)

```
AppKit NSEvent: keyCode=0x0D, modifierFlags=0x100, isARepeat=NO, characters="w"
  ↓ handler push
record: { KeyDown, keyCode=0x0D, isARepeat=false }
  ↓ NSEventDrain
scancode = kNSKeyCodeToSDLScancode[0x0D] = SDL_SCANCODE_W (26)
keyboardState[26] = true
cbStack.back()->IR_OnKeyboardPress(26)
  ↓ action handler
"move_fwd" → Actor.moveForward()
```

### 4.2. Та же физическая клавиша, RU раскладка

```
AppKit NSEvent: keyCode=0x0D, characters="ц", charactersIgnoringModifiers="ц"
                ↑ keyCode идентичен — physical key не зависит от раскладки
  ↓ всё остальное идентично 4.1 — мы не смотрим на characters
IR_OnKeyboardPress(26)
"move_fwd" → персонаж идёт вперёд
```

Биндинг storage в `user.ltx` остаётся в scancode → layout-independent.

### 4.3. Shift зажат во время удерживаемого W

```
W уже зажата (state[26]=true)
AppKit NSEvent: NSEventTypeFlagsChanged, modifierFlags=0x20102, keyCode=0x38
  ↓ shim diff
diff = 0x20102 ^ g_lastModifierFlags(0x100) = 0x20000 (Shift bit)
bit appeared → push KeyDown { keyCode=0x38 }
g_lastModifierFlags = 0x20102
  ↓ NSEventDrain
scancode = SDL_SCANCODE_LSHIFT (225)
IR_OnKeyboardPress(225)
"sprint_toggle" → бег on
```

### 4.4. Mouse motion captured

```
NSEvent type=MouseMoved, deltaX=12.3, deltaY=-4.7
shim знает g_mouseCaptured=true
  ↓ push
record { MouseMove, deltaX=12.3, deltaY=-4.7, locX=0, locY=0 }
  ↓ drain
IR_OnMouseMove(12, -4)
Camera.yaw += 12*sens; Camera.pitch -= 4*sens
```

### 4.5. Cmd+Q (graceful quit — не сломать)

```
AppKit двa NSEvent'а:
  1. FlagsChanged, modifierFlags |= Cmd
  2. KeyDown, keyCode=0x0C (Q), modifierFlags has Cmd
  ↓ handler на (2):
проверка Cmd+Q раньше чем queue push:
  keyCode==0x0C && (modifierFlags & NSEventModifierFlagCommand) → match
  → OpenXRay_RequestGracefulQuit()
  return nil  (не push'аем в queue)
```

Cmd+Q путь идентичен сегодняшнему.

## 5. Edge cases

### 5.1. Stuck-key syndrome (P1)

Cmd-Tab во время удерживаемой клавиши → KeyUp ушёл другому app →
наш `keyboardState` думает что клавиша держится.

**Fix**: shim наблюдает `applicationWillResignActive:` (уже есть после
A.1) → вызывает `OpenXRay_SyntheticReleaseAllKeys()` → drain push'ит KeyUp
для каждого зажатого scancode. То же для `workspaceWillSleep:`.

На `applicationDidBecomeActive:` — flush AppKit event queue (`[NSEvent
clearLocalEvents]`) и сброс `g_lastModifierFlags` к
`[NSEvent modifierFlags]` (текущее реальное состояние модификаторов).

### 5.2. Auto-repeat (P2)

NSEvent.isARepeat → в record. В drain: `if (rec.isARepeat) skip
IR_OnKeyboardPress` (state уже true). Это идентично SDL'у который
фильтрует `event.key.repeat`.

### 5.3. CapsLock toggle (P2)

CapsLock — toggle, не momentary. На каждое изменение CapsLock-бита в
FlagsChanged → push KeyDown+KeyUp подряд (synthetic tap), идентично
SDL поведению на macOS.

### 5.4. Dead keys / IME composition (P3 — known limitation)

`Option+u` + следующая буква → буква с умлаутом. Мы биндим по physical
key, не по символу — для геймплея неактуально. Консоль движка
использует SDL_TEXTINPUT параллельным путём — он остаётся через SDL
(текст ввод в консоль и так неполноценен на русском — отдельный issue
если станет блокером).

### 5.5. Mouse outside window при capture (P2)

NSEvent продолжает шлёт deltas даже когда курсор «за окном» —
delta-аккумулятор валиден. AppKit прячет курсор + warp в центр (это
уже делает текущий код через `CGWarpMouseCursorPosition`).

### 5.6. Multi-monitor / backing scale change (P3)

`backingScaleFactor` кэшируется в shim'е. AppKit шлёт
`windowDidChangeBackingProperties:` при переезде окна между мониторами —
наш `OpenXRayCocoaShim` это обрабатывает, обновляет кэш.

### 5.7. Queue overflow (P3)

Ring 256 переполняется только при main loop stall >1с. Drop oldest +
`Msg("! NS event queue overflow")` через one-shot static bool.

### 5.8. Cvar `nsevent_input` flip в рантайме (P2)

Cvar setter callback (в `xr_input.cpp` под Apple-gate) делает три шага
**в любом направлении flip'а**:

1. `OpenXRay_SyntheticReleaseAllKeys()` — `CInput` итерируется по своему
   `keyboardState`, для каждого зажатого scancode вызывает
   `cbStack.back()->IR_OnKeyboardRelease(scancode)`, обнуляет state
2. Сбросить mouse-button state аналогично (через `IR_OnMousePressRelease`)
3. Обновить `g_nsEventInputEnabled` в shim'е

Это идемпотентный soft-reset — не важно через какой pipeline пришёл
KeyDown'ы, на момент flip'а движок считает что всё отпущено. Документ
как «cvar flip в рантайме = soft reset input state».

### 5.9. Loading screen (P3)

Push продолжается во время loading (handler не блокируется). Drain
происходит когда engine вернётся к OnFrame — все накопленные события
дренируются. Overflow → drop oldest (идентично SDL).

### 5.10. Modifier-only key press (P3)

Юзер нажал только Cmd. AppKit шлёт FlagsChanged → push
KeyDown(LGUI/RGUI). Engine увидит → если ничего не забиндено на
LGUI/RGUI, no-op. Идентично SDL.

## 6. Sub-steps (порядок коммитов в feature-ветке)

A.3 имплементируется в 4 коммитах в одной feature-ветке
`issue-NNN-a3-nsevent-input` (NNN — номер gitea issue после создания):

| # | Что | Файлы | Mini-smoke |
|---|---|---|---|
| **A.3.1** | Static keyCode→scancode table + dev verify command | `xr_input.cpp` (новый блок под `#if XR_PLATFORM_APPLE`), регистрация консольной команды | `dev_tools 1` + `verify_input_table` → 0 diff против `SDL_GetScancodeFromKey` для всех keys из default bindings |
| **A.3.2** | Cvar `nsevent_input` + NSEvent local monitor + queue + drain для **клавиатуры**. Handler consume'ит keyboard events когда `cvar==1`; SDL KeyUpdate под Apple-gate'ом становится no-op (нечего drain'ить — event'ы consumed). `OpenXRay_SyntheticReleaseAllKeys()` готов и вызывается на cvar flip. | `xrRender_console.cpp` (cvar регистрация под `#if XR_PLATFORM_APPLE`), `macos_cocoa_shim.mm`, `macos_cocoa_shim.h`, `xr_input.cpp` (NSEventDrain в OnFrame + cvar setter callback) | WASD + Shift+W + Esc работают; flip `nsevent_input 0` под удерживаемой W → нет stuck (через SDL продолжает); flip обратно 1 → нет stuck (synthetic release переключил состояние) |
| **A.3.3** | Расширение mask + drain для **мыши** + scroll. Mouse capture-mode bridge (`OpenXRay_SetMouseCaptureMode`). SDL MouseUpdate под Apple-gate'ом становится no-op. | `macos_cocoa_shim.mm`, `xr_input.cpp` (mouse translate + capture bridge) | Look around captured режим; меню — курсор виден на Retina; scroll wheel переключает оружие |
| **A.3.4** | Focus-loss / sleep recovery: `applicationWillResignActive:` + `workspaceWillSleep:` дёргают `OpenXRay_SyntheticReleaseAllKeys()`. `windowDidChangeBackingProperties:` обновляет кэш `backingScaleFactor`. | `macos_cocoa_shim.mm` (добавление observers), мелкие дополнения в `xr_input.cpp` | Cmd-Tab во время удержания W → возврат → нет stuck key; sleep крышки → wake → нет stuck key; 30 min CoP soak |

После A.3.2 keyboard pipeline переключён полностью — SDL keyboard drain
становится no-op потому что local monitor consume'ит события. Никакого
«параллельного» режима нет: верификация происходит через прямой
gameplay smoke (если pipeline сломан — игра не отреагирует). Дев-команда
`verify_input_table` из A.3.1 уже подтвердила корректность scancode
маппинга оффлайн.

## 7. Smoke план (детально)

### 7.1. Per-commit smoke (~3 минуты после каждого commit'а)

```
1. make build-release && auto-ship отработал
2. /Applications/OpenXRay-Dev.app → main menu
3. New Game → Cordon
4. 60 секунд gameplay: WASD, jump (Space), shoot, inventory (I),
   weapons (1/2/3), inspect (V)
5. Cmd+Q → graceful quit
6. tail ~/Library/Logs/OpenXRay/openxray.log → no new ERROR/WARN
```

### 7.2. Gate verification (полный чеклист «A.3 done»)

#### Keyboard
- [ ] WASD движение (US раскладка)
- [ ] WASD движение (RU раскладка — физическая позиция, не символ)
- [ ] Shift+W бег; отпускание модификаторов чистое
- [ ] Esc открывает/закрывает паузу
- [ ] Q/E lean
- [ ] 1-6 переключение оружия
- [ ] Tab/F/M/J прочие действия
- [ ] Auto-repeat в текстовых полях
- [ ] CapsLock toggle не ломает других модификаторов

#### Mouse
- [ ] Mouse capture в геймплее: курсор скрыт
- [ ] Look around: горизонталь+вертикаль, sensitivity ощущается прежней
- [ ] Esc → мышь освобождена, видна на Retina с правильным DPI
- [ ] Лев/прав/средн клики работают
- [ ] Scroll wheel (колесо или trackpad) — переключение оружия / inventory

#### Lifecycle
- [ ] Cmd-Tab во время удержания W → возврат → нет stuck key
- [ ] Sleep крышки во время игры → wake → нет stuck key, нет crash
- [ ] Cmd+Q graceful quit (не сломали A.1)
- [ ] Mission Control overlay → input не утекает в игру

#### Cvar rollback
- [ ] `nsevent_input 0` в рантайме под удерживаемой клавишей → нет stuck
- [ ] `nsevent_input 1` обратно → чисто

#### Final soak
- [ ] 30 минут CoP gameplay на M1 без visible regression
- [ ] No new ERROR/WARN в `openxray_ragnar.log` / `openxray.log`
- [ ] No crash, hang, stuck input
- [ ] Save → quit → relaunch → load — round-trip clean

### 7.3. Sample-hang readiness

```bash
make sample-hang
```

В stack должен быть AppKit event dispatch, не SDL_PollEvent. Если
SDL_PollEvent на main thread в момент input event'а — regression
(local monitor не consume'ит).

### 7.4. Откат

1. **Runtime**: `nsevent_input 0` в консоли → SDL pipeline без перезапуска
2. **Build-time**: `git revert <merge-sha>` + `make ship` (~30 секунд)
3. **Stable channel**: `/Applications/OpenXRay.app` — предыдущий tag всегда играем через `make rollback-stable TAG=<prev>`

## 8. Что НЕ входит в A.3

- **Controller / gamepad input** — продолжает через SDL (отдельный шаг позже)
- **IME / dead-key composition в консоли** — known limitation, текстовый русский ввод неполноценен; отдельный issue если станет блокером
- **Trackpad pinch / swipe gestures** — движок не использует
- **Tablet / Pencil input** — out of scope
- **UI hint strings показывающие symbol для текущей раскладки** («Стрельба: Ф») — UX-фича на основе reverse lookup, не требуется для геймплея
- **A.2-rest (NSRunLoop + CADisplayLink как single source of truth)** — отдельный шаг (логически между A.3 и A.7)

## 9. Связано

- Roadmap: [`2026-05-25-native-shell-roadmap.md`](2026-05-25-native-shell-roadmap.md) §3
- Hang taxonomy: [`notes/decisions/hang-taxonomy.md`](../../../notes/decisions/hang-taxonomy.md) — Family 1.A (blocking I/O main) — A.3 не закрывает этот класс, A.2-rest и A.6 закроют
- Engine map: [`notes/reference/engine-map.md`](../../../notes/reference/engine-map.md) §Input/keybinds
- Cocoa shim текущий: `src/xrEngine/macos_cocoa_shim.mm:184-275`
- Input layer текущий: `src/xrEngine/xr_input.cpp:180-527`, `src/xrEngine/IInputReceiver.h:13-66`
- A.1 PR: #115 (lifecycle observers)
- A.2 PR: #118 (ghttp worker)
- A.2.1 follow-up: #119 (deferred cleanup, не блокирует A.3)
