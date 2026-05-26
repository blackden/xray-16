# A.3 NSEvent Input Pipeline — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** На macOS убрать SDL из цепочки обработки клавиатуры/мыши/скролла — события идут через NSEvent local monitor → fixed-size ring queue → `NSEventDrain()` в `CInput::OnFrame()` → существующие `IR_OnKeyboard*`/`IR_OnMouse*` callbacks. Controller (gamepad) и window events продолжают идти через SDL.

**Architecture:** Один-thread'овый producer/consumer (handler и drain оба на main thread, разнесены во времени в пределах кадра — без atomics). NSEvent keyCode → SDL_Scancode через статический `constexpr` массив-словарь (репликация SDL'ового внутреннего маппинга). Cvar `nsevent_input` (default 1) даёт runtime rollback на SDL pipeline. Focus loss / sleep / cvar flip триггерят `SyntheticReleaseAllKeys` чтобы избежать stuck keys.

**Tech Stack:** C++17, Objective-C++ (.mm), AppKit (NSEvent, NSWorkspace observers), `<Carbon/HIToolbox/Events.h>` (kVK_* keyCode constants), CMake, существующий xrInput layer, `OpenXRayCocoaShim` из A.1.

**Spec:** [`docs/superpowers/specs/2026-05-26-a3-nsevent-input-design.md`](../specs/2026-05-26-a3-nsevent-input-design.md) (commit `de9e4a4b8`)

**Roadmap:** [`docs/superpowers/specs/2026-05-25-native-shell-roadmap.md`](../specs/2026-05-25-native-shell-roadmap.md) §3 шаг A.3 эпика [#87](https://git.fedorov.tech/ragnar/xray-16/issues/87)

**Branch base:** `macos/blackden/master` (long-running integration branch — не `dev`)

**Build constraint (memory):** ОДИН `make build-release` за раз, без parallel cmake вызовов, без retry на ошибках. Cmake cache contention → hang.

---

## Phase 0: Setup и adversarial review

Этот phase делает foreground claude (не apple-platform agent).

### Task 0.1: Создать gitea issue для A.3

**Files:** none (gitea-only)

- [ ] **Step 1: Создать issue в gitea**

Использовать MCP `mcp__gitea__issue_write` с body:

```markdown
## Что

A.3: NSEvent input pipeline — клавиатура / мышь / скролл / modifier flags
идут через NSEvent local monitor (не SDL_PollEvent) на macOS. Маппинг
NSEvent keyCode → SDL_Scancode через статический массив; controller и
window events остаются через SDL.

## Контекст

Третий шаг направления A (native macOS shell, эпик #87). После
A.1 (#114→PR #115, lifecycle observers) и A.2 (#117→PR #118, ghttp
dispatch_queue worker). Roadmap: `docs/superpowers/specs/2026-05-25-native-shell-roadmap.md` §3.

## Дизайн

`docs/superpowers/specs/2026-05-26-a3-nsevent-input-design.md`
(commit de9e4a4b8) — архитектура, sub-step разбиение, edge cases,
smoke план.

## План имплементации

`docs/superpowers/plans/2026-05-26-a3-nsevent-input.md`

## Гейт

- WASD на eng+rus раскладке (физический keyCode идентичен)
- Shift+W бег; модификаторы чистые
- Cmd+Q graceful quit (не сломать A.1)
- Cmd-Tab во время удержания → нет stuck key
- Mouse capture/release корректный (Retina backing scale 2x)
- `nsevent_input` cvar flip в рантайме = soft reset, нет stuck
- 30 min CoP soak на M1 без regression

## Связано

- Эпик: #87
- Roadmap: PR #107 (изначальное A/B/C решение)
- A.2 follow-up: #119 (не блокирует A.3)
```

Заголовок: `A.3: NSEvent input pipeline — keyboard/mouse/scroll через NSEvent`

Запомнить номер issue → `120`.

- [ ] **Step 2: Зафиксировать номер issue в плане**

Заменить `120` в этом документе на фактический номер (через Edit tool).

### Task 0.2: Создать feature-ветку

**Files:** none (git-only)

- [ ] **Step 1: Branch off от macos/blackden/master**

```bash
git fetch
git checkout macos/blackden/master
git pull --ff-only
git checkout -b issue-120-a3-nsevent-input
```

- [ ] **Step 2: Verify branch state**

```bash
git status
git log --oneline -3
```

Expected: clean working tree, HEAD на свежем `macos/blackden/master`.

### Task 0.3: Adversarial review плана через team-lead агента

**Files:** none (review-only)

- [ ] **Step 1: Dispatch team-lead агента с этим планом + spec'ом**

Промпт для team-lead:

```
Adversarial review плана A.3 NSEvent input pipeline.

Spec: docs/superpowers/specs/2026-05-26-a3-nsevent-input-design.md (de9e4a4b8)
Plan: docs/superpowers/plans/2026-05-26-a3-nsevent-input.md
Roadmap: docs/superpowers/specs/2026-05-25-native-shell-roadmap.md §3
Memory ключевое: feedback_premise_audit_roadmap_steps,
feedback_correctness_over_throughput, feedback_agent_build_serial.

Состояние: A.1 + A.2 merged, главный цикл всё ещё SDL pump
(оригинальный A.2 NSRunLoop swap отложен). Этот шаг отрезает
keyboard/mouse/scroll от SDL без замены main loop.

Хочу adversarial вид на:

1. Премис-аудит: гейт «A.2 done» зелёный для целей A.3? Family 1.A
   ghttp mitigation достаточна или A.3 должен сначала закрыть остатки
   blocking-I/O класса?
2. Sub-step ordering: A.3.1 → A.3.2 → A.3.3 → A.3.4 — нет ли
   inversion'а зависимостей? Например, cvar в A.3.2 без
   focus-loss recovery (которое в A.3.4) — это безопасно для
   incremental ship'а после каждого sub-step'а?
3. Risk concentration: какой sub-step самый опасный по reg risk'у и
   как изолировать?
4. Out-of-scope честно? IME / dead-keys / controller — реально не
   нужны для A.3 done gate'а или мы себя обманываем?

Report под 400 слов. Если есть P0/P1 issue с планом — назови
конкретно (task #, что менять). Если ок — дай зелёный свет.
```

- [ ] **Step 2: Применить feedback от team-lead'а**

Если team-lead вернул конкретные изменения (P0/P1) — внести в этот документ через Edit, перепроверить spec consistency. Если зелёный свет — переходим к Phase 1.

---

## Phase 1: A.3.1 — Static keyCode→scancode table + dev verify

**Owner:** apple-platform agent (dispatch одной задачей с инструкциями ниже).

**Smoke gate:** `dev_tools 1` + консольная команда `verify_input_table` → лог пишет `0 diff` для всех scancode'ов из default bindings.

### Task 1.1: Добавить статический массив `kNSKeyCodeToSDLScancode` в `xr_input.cpp`

**Files:**
- Modify: `src/xrEngine/xr_input.cpp` (новый блок под `#if XR_PLATFORM_APPLE`)

- [ ] **Step 1: Прочитать существующую структуру `xr_input.cpp`**

Read: `src/xrEngine/xr_input.cpp:1-50` (headers + namespace) и `:775-791` (`OnFrame()`).

Цель: понять куда вставлять `#include <Carbon/HIToolbox/Events.h>` (после существующих SDL/system includes) и куда class-static таблицу.

- [ ] **Step 2: Добавить include**

В блоке includes (после `#include "Config.hpp"` и SDL includes), под Apple-gate:

```cpp
#if defined(XR_PLATFORM_APPLE)
#include <Carbon/HIToolbox/Events.h>
#endif
```

- [ ] **Step 3: Добавить таблицу — namespace-level constexpr под Apple-gate**

После включений, перед namespace/class definitions:

```cpp
#if defined(XR_PLATFORM_APPLE)
// Apple HID keyCode → SDL_Scancode mapping.
// Static replica of SDL's internal table in SDL_cocoakeyboard.m. Lets us
// drop SDL from the keyboard event path without changing the engine's
// scancode-based binding system (xr_level_controller.cpp).
// Indices match NSEvent.keyCode values (kVK_* constants from
// <Carbon/HIToolbox/Events.h>). Unmapped entries default to
// SDL_SCANCODE_UNKNOWN.
namespace {
constexpr SDL_Scancode kNSKeyCodeToSDLScancode[128] = {
    [kVK_ANSI_A]              = SDL_SCANCODE_A,
    [kVK_ANSI_S]              = SDL_SCANCODE_S,
    [kVK_ANSI_D]              = SDL_SCANCODE_D,
    [kVK_ANSI_F]              = SDL_SCANCODE_F,
    [kVK_ANSI_H]              = SDL_SCANCODE_H,
    [kVK_ANSI_G]              = SDL_SCANCODE_G,
    [kVK_ANSI_Z]              = SDL_SCANCODE_Z,
    [kVK_ANSI_X]              = SDL_SCANCODE_X,
    [kVK_ANSI_C]              = SDL_SCANCODE_C,
    [kVK_ANSI_V]              = SDL_SCANCODE_V,
    [kVK_ANSI_B]              = SDL_SCANCODE_B,
    [kVK_ANSI_Q]              = SDL_SCANCODE_Q,
    [kVK_ANSI_W]              = SDL_SCANCODE_W,
    [kVK_ANSI_E]              = SDL_SCANCODE_E,
    [kVK_ANSI_R]              = SDL_SCANCODE_R,
    [kVK_ANSI_Y]              = SDL_SCANCODE_Y,
    [kVK_ANSI_T]              = SDL_SCANCODE_T,
    [kVK_ANSI_1]              = SDL_SCANCODE_1,
    [kVK_ANSI_2]              = SDL_SCANCODE_2,
    [kVK_ANSI_3]              = SDL_SCANCODE_3,
    [kVK_ANSI_4]              = SDL_SCANCODE_4,
    [kVK_ANSI_6]              = SDL_SCANCODE_6,
    [kVK_ANSI_5]              = SDL_SCANCODE_5,
    [kVK_ANSI_Equal]          = SDL_SCANCODE_EQUALS,
    [kVK_ANSI_9]              = SDL_SCANCODE_9,
    [kVK_ANSI_7]              = SDL_SCANCODE_7,
    [kVK_ANSI_Minus]          = SDL_SCANCODE_MINUS,
    [kVK_ANSI_8]              = SDL_SCANCODE_8,
    [kVK_ANSI_0]              = SDL_SCANCODE_0,
    [kVK_ANSI_RightBracket]   = SDL_SCANCODE_RIGHTBRACKET,
    [kVK_ANSI_O]              = SDL_SCANCODE_O,
    [kVK_ANSI_U]              = SDL_SCANCODE_U,
    [kVK_ANSI_LeftBracket]    = SDL_SCANCODE_LEFTBRACKET,
    [kVK_ANSI_I]              = SDL_SCANCODE_I,
    [kVK_ANSI_P]              = SDL_SCANCODE_P,
    [kVK_Return]              = SDL_SCANCODE_RETURN,
    [kVK_ANSI_L]              = SDL_SCANCODE_L,
    [kVK_ANSI_J]              = SDL_SCANCODE_J,
    [kVK_ANSI_Quote]          = SDL_SCANCODE_APOSTROPHE,
    [kVK_ANSI_K]              = SDL_SCANCODE_K,
    [kVK_ANSI_Semicolon]      = SDL_SCANCODE_SEMICOLON,
    [kVK_ANSI_Backslash]      = SDL_SCANCODE_BACKSLASH,
    [kVK_ANSI_Comma]          = SDL_SCANCODE_COMMA,
    [kVK_ANSI_Slash]          = SDL_SCANCODE_SLASH,
    [kVK_ANSI_N]              = SDL_SCANCODE_N,
    [kVK_ANSI_M]              = SDL_SCANCODE_M,
    [kVK_ANSI_Period]         = SDL_SCANCODE_PERIOD,
    [kVK_Tab]                 = SDL_SCANCODE_TAB,
    [kVK_Space]               = SDL_SCANCODE_SPACE,
    [kVK_ANSI_Grave]          = SDL_SCANCODE_GRAVE,
    [kVK_Delete]              = SDL_SCANCODE_BACKSPACE,
    [kVK_Escape]              = SDL_SCANCODE_ESCAPE,
    [kVK_Command]             = SDL_SCANCODE_LGUI,
    [kVK_Shift]                = SDL_SCANCODE_LSHIFT,
    [kVK_CapsLock]            = SDL_SCANCODE_CAPSLOCK,
    [kVK_Option]              = SDL_SCANCODE_LALT,
    [kVK_Control]             = SDL_SCANCODE_LCTRL,
    [kVK_RightCommand]        = SDL_SCANCODE_RGUI,
    [kVK_RightShift]          = SDL_SCANCODE_RSHIFT,
    [kVK_RightOption]         = SDL_SCANCODE_RALT,
    [kVK_RightControl]        = SDL_SCANCODE_RCTRL,
    [kVK_ANSI_KeypadDecimal]  = SDL_SCANCODE_KP_PERIOD,
    [kVK_ANSI_KeypadMultiply] = SDL_SCANCODE_KP_MULTIPLY,
    [kVK_ANSI_KeypadPlus]     = SDL_SCANCODE_KP_PLUS,
    [kVK_ANSI_KeypadClear]    = SDL_SCANCODE_NUMLOCKCLEAR,
    [kVK_ANSI_KeypadDivide]   = SDL_SCANCODE_KP_DIVIDE,
    [kVK_ANSI_KeypadEnter]    = SDL_SCANCODE_KP_ENTER,
    [kVK_ANSI_KeypadMinus]    = SDL_SCANCODE_KP_MINUS,
    [kVK_ANSI_KeypadEquals]   = SDL_SCANCODE_KP_EQUALS,
    [kVK_ANSI_Keypad0]        = SDL_SCANCODE_KP_0,
    [kVK_ANSI_Keypad1]        = SDL_SCANCODE_KP_1,
    [kVK_ANSI_Keypad2]        = SDL_SCANCODE_KP_2,
    [kVK_ANSI_Keypad3]        = SDL_SCANCODE_KP_3,
    [kVK_ANSI_Keypad4]        = SDL_SCANCODE_KP_4,
    [kVK_ANSI_Keypad5]        = SDL_SCANCODE_KP_5,
    [kVK_ANSI_Keypad6]        = SDL_SCANCODE_KP_6,
    [kVK_ANSI_Keypad7]        = SDL_SCANCODE_KP_7,
    [kVK_ANSI_Keypad8]        = SDL_SCANCODE_KP_8,
    [kVK_ANSI_Keypad9]        = SDL_SCANCODE_KP_9,
    [kVK_F5]                  = SDL_SCANCODE_F5,
    [kVK_F6]                  = SDL_SCANCODE_F6,
    [kVK_F7]                  = SDL_SCANCODE_F7,
    [kVK_F3]                  = SDL_SCANCODE_F3,
    [kVK_F8]                  = SDL_SCANCODE_F8,
    [kVK_F9]                  = SDL_SCANCODE_F9,
    [kVK_F11]                 = SDL_SCANCODE_F11,
    [kVK_F13]                 = SDL_SCANCODE_PRINTSCREEN,
    [kVK_F14]                 = SDL_SCANCODE_SCROLLLOCK,
    [kVK_F10]                 = SDL_SCANCODE_F10,
    [kVK_F12]                 = SDL_SCANCODE_F12,
    [kVK_F15]                 = SDL_SCANCODE_PAUSE,
    [kVK_Help]                = SDL_SCANCODE_INSERT,
    [kVK_Home]                = SDL_SCANCODE_HOME,
    [kVK_PageUp]              = SDL_SCANCODE_PAGEUP,
    [kVK_ForwardDelete]       = SDL_SCANCODE_DELETE,
    [kVK_F4]                  = SDL_SCANCODE_F4,
    [kVK_End]                 = SDL_SCANCODE_END,
    [kVK_F2]                  = SDL_SCANCODE_F2,
    [kVK_PageDown]            = SDL_SCANCODE_PAGEDOWN,
    [kVK_F1]                  = SDL_SCANCODE_F1,
    [kVK_LeftArrow]           = SDL_SCANCODE_LEFT,
    [kVK_RightArrow]          = SDL_SCANCODE_RIGHT,
    [kVK_DownArrow]           = SDL_SCANCODE_DOWN,
    [kVK_UpArrow]              = SDL_SCANCODE_UP,
};
} // namespace
#endif
```

- [ ] **Step 4: Build (SERIAL — один вызов, без retry)**

```bash
make build-release
```

Expected: exit 0, артефакт `bin/arm64/ReleaseMasterGold/xr_3da` свежий. Если warning'и про unused — игнорировать (таблица используется позже).

При ошибке компиляции: прочитать стек, поправить, **один повторный** `make build-release`. Не больше.

- [ ] **Step 5: Не коммитить — таблица будет использована вместе с verify command в одном коммите**

### Task 1.2: Добавить дев-команду `verify_input_table`

**Files:**
- Modify: `src/xrEngine/xr_input.cpp` (новая функция + регистрация консольной команды)

- [ ] **Step 1: Найти где регистрируются другие dev-команды**

Read engine-map для входа в console registration; конкретно команды под `dev_tools` cvar registered в `src/xrEngine/Console_Commands.cpp` (verify location). Использовать `mcp__lumen__semantic_search` если нужно.

- [ ] **Step 2: Добавить функцию `verify_input_table` в `xr_input.cpp` под Apple-gate**

После таблицы из Task 1.1, под `#if defined(XR_PLATFORM_APPLE)`:

```cpp
namespace {
void VerifyInputTable()
{
    int total = 0;
    int matches = 0;
    int mismatches = 0;
    int unmapped = 0;

    for (int keyCode = 0; keyCode < 128; ++keyCode)
    {
        SDL_Scancode ourScancode = kNSKeyCodeToSDLScancode[keyCode];

        // SDL doesn't expose its NSEvent keyCode table directly. Approximate
        // check: SDL_GetKeyFromScancode(ourScancode) should return a valid
        // key (i.e. ourScancode is real). For unmapped entries, ourScancode
        // is SDL_SCANCODE_UNKNOWN (0).
        if (ourScancode == SDL_SCANCODE_UNKNOWN)
        {
            ++unmapped;
            continue;
        }

        SDL_Keycode key = SDL_GetKeyFromScancode(ourScancode);
        if (key == SDLK_UNKNOWN)
        {
            Msg("! verify_input_table: keyCode 0x%02x → scancode %d → no SDL key",
                keyCode, ourScancode);
            ++mismatches;
        }
        else
        {
            ++matches;
        }
        ++total;
    }

    Msg("verify_input_table: total=%d matches=%d mismatches=%d unmapped=%d",
        total + unmapped, matches, mismatches, unmapped);
}
} // namespace

extern "C" void OpenXRay_VerifyInputTable()
{
    VerifyInputTable();
}
#endif
```

- [ ] **Step 3: Найти регистрацию консольных команд для dev_tools**

Engine-map подсказывает что F6/F7 dev hotkeys gated через `dev_tools` cvar. Регистрация команд обычно в `Console_Commands.cpp` или `xrEngine/Console_Commands.cpp`. Прочитать существующий шаблон регистрации простой команды (например, F-гэйтнутая или `flush`).

- [ ] **Step 4: Зарегистрировать `verify_input_table` как консольную команду**

В соответствующем файле под `#if defined(XR_PLATFORM_APPLE)`:

```cpp
class CCC_VerifyInputTable : public IConsole_Command
{
public:
    CCC_VerifyInputTable(LPCSTR N) : IConsole_Command(N) {}
    virtual void Execute(LPCSTR /*args*/) override
    {
        if (dev_tools)
            OpenXRay_VerifyInputTable();
        else
            Msg("! verify_input_table requires dev_tools 1");
    }
    virtual void Save(IWriter*) override {}
};

// в registration block:
#if defined(XR_PLATFORM_APPLE)
CMD1(CCC_VerifyInputTable, "verify_input_table");
#endif
```

Точный сайт регистрации — смотреть по соседним `CMD1(...)` вызовам в файле.

- [ ] **Step 5: Build (SERIAL)**

```bash
make build-release
```

Expected: exit 0.

- [ ] **Step 6: Smoke test — запуск и выполнение команды**

```bash
open /Applications/OpenXRay-Dev.app
# в игре открыть консоль (~), ввести:
# dev_tools 1
# verify_input_table
# quit
```

Expected в `~/.openxray-data/logs/openxray_ragnar.log`:
```
verify_input_table: total=N matches=M mismatches=0 unmapped=K
```

Где `mismatches=0` обязательно. `total ≈ 90-100`, `unmapped ≈ 28-38` (зарезервированные/unused HID slot'ы).

- [ ] **Step 7: Commit A.3.1**

```bash
git add src/xrEngine/xr_input.cpp src/xrEngine/Console_Commands.cpp
# (или фактические затронутые файлы)
git commit -m "xrEngine: add Apple keyCode→SDL_Scancode static table (#120 A.3 step 1/4)

Static replica of SDL's internal NSEvent keyCode → SDL_Scancode
mapping (SDL_cocoakeyboard.m). Indexed by NSEvent.keyCode from
<Carbon/HIToolbox/Events.h>. Lets us drop SDL from the keyboard
event path without changing the engine's scancode-based binding
system.

Adds dev-only console command 'verify_input_table' (gated by
dev_tools 1) that walks the table and confirms every mapped
entry round-trips through SDL_GetKeyFromScancode.

Step 1 of 4 toward issue #120: A.3 NSEvent input pipeline.
Spec: docs/superpowers/specs/2026-05-26-a3-nsevent-input-design.md
Plan: docs/superpowers/plans/2026-05-26-a3-nsevent-input.md"
```

---

## Phase 2: A.3.2 — Cvar + NSEvent local monitor + queue + drain для клавиатуры

**Owner:** apple-platform agent.

**Smoke gate:** WASD ходьба (US+RU layouts), Shift+W бег, Esc меню, Cmd+Q quit, `nsevent_input 0`/`1` flip под удерживаемой клавишей — нет stuck.

### Task 2.1: Расширить `macos_cocoa_shim.h` (struct + C entries)

**Files:**
- Modify: `src/xrEngine/macos_cocoa_shim.h`

- [ ] **Step 1: Прочитать существующий header**

Read `src/xrEngine/macos_cocoa_shim.h` целиком. Понять existing extern "C" declarations + style.

- [ ] **Step 2: Добавить enum + struct + declarations**

Добавить в header перед уже существующими declarations:

```cpp
#ifdef __cplusplus
#include <cstdint>
#include <cstddef>
#endif

enum OpenXRayNSEventKind
{
    OXR_NS_EVENT_KEY_DOWN = 0,
    OXR_NS_EVENT_KEY_UP = 1,
    OXR_NS_EVENT_FLAGS_CHANGED = 2,
    OXR_NS_EVENT_MOUSE_MOVE = 3,
    OXR_NS_EVENT_MOUSE_DOWN = 4,
    OXR_NS_EVENT_MOUSE_UP = 5,
    OXR_NS_EVENT_MOUSE_DRAGGED = 6,
    OXR_NS_EVENT_SCROLL_WHEEL = 7,
};

struct OpenXRayNSEventRecord
{
    int      kind;          // OpenXRayNSEventKind
    uint16_t keyCode;       // NSEvent.keyCode for key events
    uint32_t modifierFlags; // event.modifierFlags & deviceIndependentFlagsMask
    uint8_t  mouseButton;   // 0=left, 1=right, 2=middle/other
    uint8_t  isARepeat;     // NSEvent.isARepeat (0 or 1)
    float    locX, locY;    // pixel coords (already flipped + scaled)
    float    deltaX, deltaY;// mouse motion or scroll deltas
};

#ifdef __cplusplus
extern "C" {
#endif

// Установить NSEvent local monitor (вызывается из OpenXRay_InstallCocoaShim).
void OpenXRay_InstallNSEventMonitor(void);

// Включить/выключить consume режим. При false handler возвращает event'ы
// дальше по цепочке (SDL обрабатывает как раньше).
void OpenXRay_SetNSEventInputEnabled(int enabled);

// Установить mouse capture mode (вызывается из CInput при capture/release).
// При captured=1 handler пишет deltas в record (locX/Y=0); при captured=0
// пишет absolute pixel coords (locX/Y), deltas=0.
void OpenXRay_SetMouseCaptureMode(int captured);

// Дренировать очередь NSEvent'ов в массив out (capacity maxCount).
// Возвращает фактическое число записанных событий.
size_t OpenXRay_DrainNSEventQueue(struct OpenXRayNSEventRecord* out, size_t maxCount);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 3: Build (SERIAL) — проверить компиляцию header'а**

```bash
make build-release
```

Expected: exit 0 (могут быть warning'и про unused functions — declarations пока без definitions; будут реализованы в Task 2.2).

Если build падает на «implicit declaration» — значит .mm не подхватил новые declarations. Проверить порядок include'ов в `macos_cocoa_shim.mm`.

### Task 2.2: Реализовать ring queue + handler + C entries в `macos_cocoa_shim.mm`

**Files:**
- Modify: `src/xrEngine/macos_cocoa_shim.mm`

- [ ] **Step 1: Прочитать current shim**

Read `src/xrEngine/macos_cocoa_shim.mm:1-280` (whole file). Понять:
- Где живёт current Cmd+Q local monitor (line ~208-223)
- Структура `OpenXRayCocoaShim` class
- Как вызывается `OpenXRay_InstallCocoaShim()` и установка observer'ов в `OpenXRay_ArmLifecycleObservers()`

- [ ] **Step 2: Добавить state — ring queue + flags**

В начале .mm файла (после includes), под file-static scope:

```objc
namespace {
constexpr size_t kQueueCapacity = 256;

struct NSEventQueue {
    OpenXRayNSEventRecord buf[kQueueCapacity];
    size_t head = 0;       // next read
    size_t tail = 0;       // next write
    size_t count = 0;
    bool   overflowed = false;
};

NSEventQueue g_nsEventQueue;
bool         g_nsEventInputEnabled = true;
bool         g_mouseCaptured = false;
uint32_t     g_lastModifierFlags = 0;
float        g_backingScaleFactor = 1.0f;
id           g_nsEventMonitor = nil;

void QueuePush(const OpenXRayNSEventRecord& rec)
{
    if (g_nsEventQueue.count == kQueueCapacity)
    {
        // Drop oldest
        g_nsEventQueue.head = (g_nsEventQueue.head + 1) % kQueueCapacity;
        --g_nsEventQueue.count;

        static bool warned = false;
        if (!warned)
        {
            Msg("! OpenXRay NSEvent queue overflow — dropping oldest");
            warned = true;
        }
        g_nsEventQueue.overflowed = true;
    }
    g_nsEventQueue.buf[g_nsEventQueue.tail] = rec;
    g_nsEventQueue.tail = (g_nsEventQueue.tail + 1) % kQueueCapacity;
    ++g_nsEventQueue.count;
}
} // namespace
```

`Msg` уже доступен через existing includes; если нет — добавить `#include "xrCore/log.h"`.

- [ ] **Step 3: Helper-функция для конвертации NSEvent → record**

```objc
namespace {

OpenXRayNSEventRecord MakeRecordFromKey(NSEvent* event, int kind)
{
    OpenXRayNSEventRecord rec = {};
    rec.kind = kind;
    rec.keyCode = (uint16_t)[event keyCode];
    rec.modifierFlags = (uint32_t)([event modifierFlags] & NSEventModifierFlagDeviceIndependentFlagsMask);
    rec.isARepeat = [event isARepeat] ? 1 : 0;
    return rec;
}

OpenXRayNSEventRecord MakeRecordFromFlags(NSEvent* event)
{
    OpenXRayNSEventRecord rec = {};
    rec.kind = OXR_NS_EVENT_FLAGS_CHANGED;
    rec.keyCode = (uint16_t)[event keyCode];
    rec.modifierFlags = (uint32_t)([event modifierFlags] & NSEventModifierFlagDeviceIndependentFlagsMask);
    return rec;
}

OpenXRayNSEventRecord MakeRecordFromMouse(NSEvent* event, int kind, NSWindow* window)
{
    OpenXRayNSEventRecord rec = {};
    rec.kind = kind;
    rec.modifierFlags = (uint32_t)([event modifierFlags] & NSEventModifierFlagDeviceIndependentFlagsMask);

    NSEventType t = [event type];
    if (t == NSEventTypeLeftMouseDown || t == NSEventTypeLeftMouseUp || t == NSEventTypeLeftMouseDragged)
        rec.mouseButton = 0;
    else if (t == NSEventTypeRightMouseDown || t == NSEventTypeRightMouseUp || t == NSEventTypeRightMouseDragged)
        rec.mouseButton = 1;
    else
        rec.mouseButton = 2;

    if (g_mouseCaptured)
    {
        rec.deltaX = (float)[event deltaX];
        rec.deltaY = (float)[event deltaY];
        rec.locX = 0.0f;
        rec.locY = 0.0f;
    }
    else
    {
        NSPoint loc = [event locationInWindow];
        CGFloat windowHeightPt = window
            ? window.contentLayoutRect.size.height
            : [[NSScreen mainScreen] frame].size.height;
        rec.locX = (float)(loc.x * g_backingScaleFactor);
        rec.locY = (float)((windowHeightPt - loc.y) * g_backingScaleFactor);
        rec.deltaX = 0.0f;
        rec.deltaY = 0.0f;
    }
    return rec;
}

OpenXRayNSEventRecord MakeRecordFromScroll(NSEvent* event)
{
    OpenXRayNSEventRecord rec = {};
    rec.kind = OXR_NS_EVENT_SCROLL_WHEEL;
    rec.deltaX = (float)[event scrollingDeltaX];
    rec.deltaY = (float)[event scrollingDeltaY];
    return rec;
}

} // namespace
```

- [ ] **Step 4: Реализовать `OpenXRay_InstallNSEventMonitor`**

Внутри .mm после Cmd+Q monitor block:

```objc
extern "C" void OpenXRay_InstallNSEventMonitor(void)
{
    if (g_nsEventMonitor != nil)
        return;

    NSEventMask mask =
        NSEventMaskKeyDown | NSEventMaskKeyUp | NSEventMaskFlagsChanged |
        NSEventMaskMouseMoved |
        NSEventMaskLeftMouseDown  | NSEventMaskLeftMouseUp  | NSEventMaskLeftMouseDragged  |
        NSEventMaskRightMouseDown | NSEventMaskRightMouseUp | NSEventMaskRightMouseDragged |
        NSEventMaskOtherMouseDown | NSEventMaskOtherMouseUp | NSEventMaskOtherMouseDragged |
        NSEventMaskScrollWheel;

    g_nsEventMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:mask
        handler:^NSEvent*(NSEvent* event) {
            NSEventType t = [event type];

            // Cmd+Q has priority — existing graceful quit path
            if (t == NSEventTypeKeyDown && [event keyCode] == kVK_ANSI_Q &&
                ([event modifierFlags] & NSEventModifierFlagCommand))
            {
                OpenXRay_RequestGracefulQuit();
                return nil;
            }

            if (!g_nsEventInputEnabled)
                return event;  // rollback path — SDL handles

            NSWindow* window = [event window];

            switch (t)
            {
                case NSEventTypeKeyDown:
                    QueuePush(MakeRecordFromKey(event, OXR_NS_EVENT_KEY_DOWN));
                    return nil;
                case NSEventTypeKeyUp:
                    QueuePush(MakeRecordFromKey(event, OXR_NS_EVENT_KEY_UP));
                    return nil;
                case NSEventTypeFlagsChanged:
                    QueuePush(MakeRecordFromFlags(event));
                    return nil;
                case NSEventTypeMouseMoved:
                    QueuePush(MakeRecordFromMouse(event, OXR_NS_EVENT_MOUSE_MOVE, window));
                    return nil;
                case NSEventTypeLeftMouseDown:
                case NSEventTypeRightMouseDown:
                case NSEventTypeOtherMouseDown:
                    QueuePush(MakeRecordFromMouse(event, OXR_NS_EVENT_MOUSE_DOWN, window));
                    return nil;
                case NSEventTypeLeftMouseUp:
                case NSEventTypeRightMouseUp:
                case NSEventTypeOtherMouseUp:
                    QueuePush(MakeRecordFromMouse(event, OXR_NS_EVENT_MOUSE_UP, window));
                    return nil;
                case NSEventTypeLeftMouseDragged:
                case NSEventTypeRightMouseDragged:
                case NSEventTypeOtherMouseDragged:
                    QueuePush(MakeRecordFromMouse(event, OXR_NS_EVENT_MOUSE_DRAGGED, window));
                    return nil;
                case NSEventTypeScrollWheel:
                    QueuePush(MakeRecordFromScroll(event));
                    return nil;
                default:
                    return event;
            }
        }];

    if (g_nsEventMonitor == nil)
    {
        Msg("! OpenXRay_InstallNSEventMonitor: addLocalMonitorForEventsMatchingMask returned nil");
    }
    else
    {
        Msg("OpenXRay: NSEvent input monitor installed (queue capacity %zu)", kQueueCapacity);
    }
}
```

**ВАЖНО**: handler инкорпорирует ВСЕ event типы (keyboard + mouse + scroll), хотя в A.3.2 мы используем только keyboard. Это правильно — мы строим инфраструктуру однажды; в A.3.2 keyboard records будут drain'иться через `IR_OnKeyboard*`, в A.3.3 mouse/scroll начнут drain'иться. Сами records в очереди безвредны.

- [ ] **Step 5: Реализовать остальные C entries**

```objc
extern "C" void OpenXRay_SetNSEventInputEnabled(int enabled)
{
    g_nsEventInputEnabled = (enabled != 0);
    Msg("OpenXRay: NSEvent input %s", g_nsEventInputEnabled ? "enabled" : "disabled");
}

extern "C" void OpenXRay_SetMouseCaptureMode(int captured)
{
    g_mouseCaptured = (captured != 0);
}

extern "C" size_t OpenXRay_DrainNSEventQueue(struct OpenXRayNSEventRecord* out, size_t maxCount)
{
    size_t n = 0;
    while (g_nsEventQueue.count > 0 && n < maxCount)
    {
        out[n++] = g_nsEventQueue.buf[g_nsEventQueue.head];
        g_nsEventQueue.head = (g_nsEventQueue.head + 1) % kQueueCapacity;
        --g_nsEventQueue.count;
    }
    return n;
}
```

- [ ] **Step 6: Подключить `OpenXRay_InstallNSEventMonitor()` в существующий init**

В `OpenXRay_InstallCocoaShim()` (line ~184-237), **после** установки Cmd+Q monitor, добавить:

```objc
OpenXRay_InstallNSEventMonitor();

// Init backing scale factor from main screen as conservative default;
// will be updated on windowDidChangeBackingProperties (Phase 4).
g_backingScaleFactor = (float)[[NSScreen mainScreen] backingScaleFactor];
```

- [ ] **Step 7: Build (SERIAL)**

```bash
make build-release
```

Expected: exit 0. В случае Obj-C ошибок (block syntax, nullable warnings) — поправить.

- [ ] **Step 8: Не коммитить — нужен ещё drain в xr_input.cpp + cvar регистрация**

### Task 2.3: NSEventDrain в `CInput::OnFrame()` и cvar handling

**Files:**
- Modify: `src/xrEngine/xr_input.cpp`

- [ ] **Step 1: Прочитать `CInput::OnFrame()`, `KeyUpdate()`, `MouseUpdate()`**

Read `src/xrEngine/xr_input.cpp:180-527` целиком. Понять:
- Где `KeyUpdate` начинает (line ~257)
- Где `MouseUpdate` начинает (line ~180)
- Структура `cbStack`, как доходим до top receiver

- [ ] **Step 2: Включить header shim'а в `xr_input.cpp`**

В top includes (после Apple-gate Carbon include):

```cpp
#if defined(XR_PLATFORM_APPLE)
#include "macos_cocoa_shim.h"
#endif
```

- [ ] **Step 3: Добавить cvar `nsevent_input` declaration**

В namespace anonymous под Apple-gate (вместе с таблицей):

```cpp
#if defined(XR_PLATFORM_APPLE)
namespace {
int g_nsEventInputCvar = 1;  // default enabled
uint32_t g_lastShimModifierFlags = 0;  // state для diff'а в drain
} // namespace
#endif
```

(Сама регистрация cvar — в `xrRender_console.cpp`, Task 2.4. Здесь только storage.)

- [ ] **Step 4: Реализовать synthetic release**

В namespace anon (или в `CInput` если access нужен) под Apple-gate:

```cpp
#if defined(XR_PLATFORM_APPLE)
extern "C" void OpenXRay_SyntheticReleaseAllKeys(void)
{
    CInput* pInput = Device.seqInput.GetReceiver<CInput>();
    if (!pInput || pInput->cbStack.empty())
        return;

    IInputReceiver* receiver = pInput->cbStack.back();
    for (int sc = 0; sc < SDL_NUM_SCANCODES; ++sc)
    {
        if (pInput->iGetAsyncKeyState((int)sc))
        {
            receiver->IR_OnKeyboardRelease((int)sc);
            // Mark released in our state if CInput tracks state explicitly
        }
    }
    pInput->ResetKeyboardState();  // Add this helper in CInput if not present
}
#endif
```

**Замечание**: точные имена методов (`iGetAsyncKeyState`, `ResetKeyboardState`, `cbStack` access) — сверить с существующим `xr_input.h`. Возможно понадобится `friend` или public-accessor. Если `cbStack` приватный — добавить `void IR_ReleaseAll()` метод в `CInput` который делает то же самое.

- [ ] **Step 5: Реализовать `NSEventDrain` функцию**

```cpp
#if defined(XR_PLATFORM_APPLE)

// keyCode → SDL_Scancode for modifier keys (used by FlagsChanged diff)
static SDL_Scancode ModifierKeyCodeToScancode(uint16_t keyCode)
{
    switch (keyCode)
    {
        case kVK_Shift:         return SDL_SCANCODE_LSHIFT;
        case kVK_RightShift:    return SDL_SCANCODE_RSHIFT;
        case kVK_Control:       return SDL_SCANCODE_LCTRL;
        case kVK_RightControl:  return SDL_SCANCODE_RCTRL;
        case kVK_Option:        return SDL_SCANCODE_LALT;
        case kVK_RightOption:   return SDL_SCANCODE_RALT;
        case kVK_Command:       return SDL_SCANCODE_LGUI;
        case kVK_RightCommand:  return SDL_SCANCODE_RGUI;
        case kVK_CapsLock:      return SDL_SCANCODE_CAPSLOCK;
        default:                return SDL_SCANCODE_UNKNOWN;
    }
}

void CInput::NSEventDrain()
{
    OpenXRayNSEventRecord records[64];
    size_t n;
    while ((n = OpenXRay_DrainNSEventQueue(records, 64)) > 0)
    {
        for (size_t i = 0; i < n; ++i)
        {
            const auto& r = records[i];
            if (cbStack.empty())
                continue;
            IInputReceiver* receiver = cbStack.back();

            switch (r.kind)
            {
                case OXR_NS_EVENT_KEY_DOWN:
                {
                    if (r.isARepeat)
                        break;  // auto-repeat не транслируем (как SDL filter)
                    SDL_Scancode sc = (r.keyCode < 128)
                        ? kNSKeyCodeToSDLScancode[r.keyCode]
                        : SDL_SCANCODE_UNKNOWN;
                    if (sc != SDL_SCANCODE_UNKNOWN)
                        receiver->IR_OnKeyboardPress((int)sc);
                    break;
                }
                case OXR_NS_EVENT_KEY_UP:
                {
                    SDL_Scancode sc = (r.keyCode < 128)
                        ? kNSKeyCodeToSDLScancode[r.keyCode]
                        : SDL_SCANCODE_UNKNOWN;
                    if (sc != SDL_SCANCODE_UNKNOWN)
                        receiver->IR_OnKeyboardRelease((int)sc);
                    break;
                }
                case OXR_NS_EVENT_FLAGS_CHANGED:
                {
                    uint32_t diff = r.modifierFlags ^ g_lastShimModifierFlags;
                    // CapsLock — toggle, synthesize down+up
                    if (diff & NSEventModifierFlagCapsLock)
                    {
                        receiver->IR_OnKeyboardPress((int)SDL_SCANCODE_CAPSLOCK);
                        receiver->IR_OnKeyboardRelease((int)SDL_SCANCODE_CAPSLOCK);
                    }
                    // For real modifier keys, use keyCode to disambiguate L/R
                    if (diff & (NSEventModifierFlagShift | NSEventModifierFlagControl |
                                NSEventModifierFlagOption | NSEventModifierFlagCommand))
                    {
                        SDL_Scancode sc = ModifierKeyCodeToScancode(r.keyCode);
                        if (sc != SDL_SCANCODE_UNKNOWN)
                        {
                            // determine direction: bit appeared = down, disappeared = up
                            uint32_t flagBit = 0;
                            switch (r.keyCode)
                            {
                                case kVK_Shift: case kVK_RightShift:
                                    flagBit = NSEventModifierFlagShift; break;
                                case kVK_Control: case kVK_RightControl:
                                    flagBit = NSEventModifierFlagControl; break;
                                case kVK_Option: case kVK_RightOption:
                                    flagBit = NSEventModifierFlagOption; break;
                                case kVK_Command: case kVK_RightCommand:
                                    flagBit = NSEventModifierFlagCommand; break;
                            }
                            if (flagBit && (r.modifierFlags & flagBit))
                                receiver->IR_OnKeyboardPress((int)sc);
                            else if (flagBit)
                                receiver->IR_OnKeyboardRelease((int)sc);
                        }
                    }
                    g_lastShimModifierFlags = r.modifierFlags;
                    break;
                }
                // Mouse + scroll handled in A.3.3 — для A.3.2 эти records
                // приходят в очередь но игнорируются (no-op)
                case OXR_NS_EVENT_MOUSE_MOVE:
                case OXR_NS_EVENT_MOUSE_DOWN:
                case OXR_NS_EVENT_MOUSE_UP:
                case OXR_NS_EVENT_MOUSE_DRAGGED:
                case OXR_NS_EVENT_SCROLL_WHEEL:
                    // Phase 3 — игнорируем; mouse продолжает через SDL
                    break;
            }
        }
    }
}
#endif
```

**Замечание**: `NSEventDrain` — instance метод на `CInput`. Добавить declaration в `xr_input.h` под Apple-gate.

- [ ] **Step 6: Добавить declaration `NSEventDrain` в `xr_input.h`**

В классе `CInput`:

```cpp
#if defined(XR_PLATFORM_APPLE)
public:
    void NSEventDrain();
#endif
```

- [ ] **Step 7: Подключить `NSEventDrain` в `OnFrame`**

В начале `CInput::OnFrame()` (xr_input.cpp:775):

```cpp
void CInput::OnFrame()
{
#if defined(XR_PLATFORM_APPLE)
    if (g_nsEventInputCvar)
        NSEventDrain();
#endif
    // ... existing KeyUpdate, MouseUpdate, ControllerUpdate calls
}
```

- [ ] **Step 8: Apple-gate early-return в `KeyUpdate`**

В начале `CInput::KeyUpdate()` (xr_input.cpp:257):

```cpp
void CInput::KeyUpdate()
{
#if defined(XR_PLATFORM_APPLE)
    if (g_nsEventInputCvar)
        return;  // NSEvent path active — SDL keyboard events не приходят (consumed)
#endif
    // ... existing SDL_PeepEvents loop
}
```

(MouseUpdate Apple-gate добавим в Task 3.2 — пока mouse через SDL остаётся.)

- [ ] **Step 9: Cvar setter callback для synthetic release**

Добавить функцию которая будет вызвана из console.cpp при flip'е cvar'а:

```cpp
#if defined(XR_PLATFORM_APPLE)
extern "C" void OpenXRay_OnNSEventInputCvarChanged(int newValue)
{
    int oldValue = g_nsEventInputCvar;
    g_nsEventInputCvar = newValue;

    if (oldValue != newValue)
    {
        OpenXRay_SyntheticReleaseAllKeys();
        OpenXRay_SetNSEventInputEnabled(newValue);
    }
}
#endif
```

- [ ] **Step 10: Build (SERIAL)**

```bash
make build-release
```

Expected: exit 0. Возможные ошибки:
- Access к `cbStack` private — добавить friend или accessor в `CInput`
- `iGetAsyncKeyState` / `ResetKeyboardState` имена не совпадают — поправить по факту

- [ ] **Step 11: Не коммитить — нужен cvar в console.cpp**

### Task 2.4: Регистрация cvar `nsevent_input` в `xrRender_console.cpp`

**Files:**
- Modify: `src/Layers/xrRender/xrRender_console.cpp`

- [ ] **Step 1: Прочитать существующие Apple-gated регистрации**

Read `src/Layers/xrRender/xrRender_console.cpp` целиком. Engine-map говорит cvar registration ~line 920-926.

- [ ] **Step 2: Forward-declare callback**

В top file:

```cpp
#if defined(XR_PLATFORM_APPLE)
extern "C" void OpenXRay_OnNSEventInputCvarChanged(int newValue);
#endif
```

- [ ] **Step 3: Регистрация cvar под Apple-gate**

В функции регистрации (обычно `xrRender_initconsole_commands` или похожая):

```cpp
#if defined(XR_PLATFORM_APPLE)
class CCC_NSEventInput : public CCC_Integer
{
private:
    int* m_target;
public:
    CCC_NSEventInput(LPCSTR N, int* p, int min, int max)
        : CCC_Integer(N, p, min, max), m_target(p) {}

    virtual void Execute(LPCSTR args) override
    {
        int oldValue = *m_target;
        CCC_Integer::Execute(args);
        if (*m_target != oldValue)
            OpenXRay_OnNSEventInputCvarChanged(*m_target);
    }
};

static int g_nsEventInputBacking = 1;
CMD4(CCC_NSEventInput, "nsevent_input", &g_nsEventInputBacking, 0, 1);
#endif
```

**Альтернатива** (проще): cvar напрямую через `CCC_Integer`, без custom subclass, и периодически синхронизироваться. Но callback пропускает synthetic release — это критично для no-stuck-key gate'а. Custom subclass лучше.

**ВАЖНО**: `g_nsEventInputBacking` должен быть тем же что `g_nsEventInputCvar` в `xr_input.cpp` — либо через extern, либо callback устанавливает оба. Проще: callback устанавливает `g_nsEventInputCvar` (в xr_input.cpp), а backing variable в console.cpp может быть отдельной (cvar UI value), синхронизируются через callback. Уточнить при имплементации.

- [ ] **Step 4: Build (SERIAL)**

```bash
make build-release
```

Expected: exit 0.

### Task 2.5: A.3.2 smoke + commit

- [ ] **Step 1: Ship новый бинарь**

`make ship` уже отрабатывает в post-commit hook'е, но мы пока не коммитили. Запустить вручную:

```bash
make ship
```

- [ ] **Step 2: Запустить игру**

```bash
open /Applications/OpenXRay-Dev.app
```

- [ ] **Step 3: Базовый smoke (clean log first)**

```bash
> ~/.openxray-data/logs/openxray_ragnar.log
> ~/Library/Logs/OpenXRay/openxray.log
```

Запустить игру заново. New Game → Cordon. 60 секунд:
- W (вперёд), A (влево), S (назад), D (вправо)
- Shift + W (бег)
- Прыжок (Space)
- Esc → меню → Esc → выход из меню
- Cmd+Q

- [ ] **Step 4: Проверить лог на ошибки**

```bash
grep -E '^!\\|ERROR|FATAL' ~/.openxray-data/logs/openxray_ragnar.log | head -20
grep 'NSEvent\\|nsevent' ~/.openxray-data/logs/openxray_ragnar.log | head -10
```

Expected:
- `OpenXRay: NSEvent input monitor installed (queue capacity 256)` — есть
- Нет `! OpenXRay NSEvent queue overflow`
- Нет новых ошибок vs pre-A.3 baseline

- [ ] **Step 5: Cvar flip smoke**

Снова запустить игру. New Game → Cordon. В консоли:
- Зажать W, не отпускать
- В консоль: `nsevent_input 0`
- Отпустить W
- Снова зажать W → должна продолжать работать (через SDL)
- В консоль: `nsevent_input 1`
- Отпустить W → ходьба должна остановиться

Expected: в обеих позициях персонаж реагирует на W; нет stuck-key между flip'ами.

- [ ] **Step 6: RU layout smoke**

В системных настройках macOS переключить раскладку на Русскую. Запустить игру, повторить базовый smoke (W → персонаж идёт вперёд — мы биндим по physical key, layout не должен ломать).

- [ ] **Step 7: Commit A.3.2**

```bash
git add src/xrEngine/macos_cocoa_shim.h src/xrEngine/macos_cocoa_shim.mm \
        src/xrEngine/xr_input.h src/xrEngine/xr_input.cpp \
        src/Layers/xrRender/xrRender_console.cpp
git commit -m "xrEngine: NSEvent local monitor + queue + drain для клавиатуры (#120 A.3 step 2/4)

Расширяет OpenXRayCocoaShim local monitor с Cmd+Q mask на полную
input mask (keyboard + flags + mouse + scroll). Фиксированный ring
queue [256] на main thread; producer (handler) и consumer
(CInput::NSEventDrain) разнесены во времени в пределах кадра без
atomics.

Cvar 'nsevent_input' (default 1) даёт runtime rollback на SDL
pipeline. Flip триггерит OpenXRay_SyntheticReleaseAllKeys() —
soft reset зажатых клавиш через cbStack.back()->IR_OnKeyboardRelease,
избегает stuck keys между pipeline'ами.

Этот шаг подключает только keyboard и modifier flags. Mouse +
scroll records пушатся в очередь но игнорируются в drain'е —
будут подключены в step 3/4. SDL KeyUpdate под Apple-gate'ом
становится no-op (нет events — consumed handler'ом).

Step 2 of 4 toward issue #120: A.3 NSEvent input pipeline.
Spec: docs/superpowers/specs/2026-05-26-a3-nsevent-input-design.md
Plan: docs/superpowers/plans/2026-05-26-a3-nsevent-input.md"
```

---

## Phase 3: A.3.3 — Mouse + scroll integration

**Owner:** apple-platform agent.

**Smoke gate:** Look around в captured режиме; курсор в меню на Retina; scroll wheel переключает оружие.

### Task 3.1: Mouse + scroll translation в `CInput::NSEventDrain`

**Files:**
- Modify: `src/xrEngine/xr_input.cpp`

- [ ] **Step 1: Прочитать существующий `MouseUpdate` для понимания IR_On* mouse callbacks**

Read `src/xrEngine/xr_input.cpp:180-255`. Понять:
- `IR_OnMousePress(button)` / `IR_OnMouseRelease(button)` / `IR_OnMouseHold(button)`
- `IR_OnMouseMove(x, y)` — что значит x/y для captured vs non-captured
- `IR_OnMouseWheel(x, y)` — какие масштабы delta

- [ ] **Step 2: Расширить switch в `NSEventDrain` — обработать mouse/scroll**

Заменить `case OXR_NS_EVENT_MOUSE_*: break;` блоки на:

```cpp
case OXR_NS_EVENT_MOUSE_MOVE:
case OXR_NS_EVENT_MOUSE_DRAGGED:
{
    // captured: deltaX/deltaY; non-captured: locX/locY
    int dx, dy;
    if (r.deltaX != 0.0f || r.deltaY != 0.0f)
    {
        dx = (int)r.deltaX;
        dy = (int)r.deltaY;
    }
    else
    {
        dx = (int)r.locX;
        dy = (int)r.locY;
    }
    receiver->IR_OnMouseMove(dx, dy);
    break;
}
case OXR_NS_EVENT_MOUSE_DOWN:
    receiver->IR_OnMousePress((int)r.mouseButton);
    break;
case OXR_NS_EVENT_MOUSE_UP:
    receiver->IR_OnMouseRelease((int)r.mouseButton);
    break;
case OXR_NS_EVENT_SCROLL_WHEEL:
    receiver->IR_OnMouseWheel((int)r.deltaX, (int)r.deltaY);
    break;
```

- [ ] **Step 3: Apple-gate early-return в `MouseUpdate`**

В начале `CInput::MouseUpdate()` (xr_input.cpp:180):

```cpp
void CInput::MouseUpdate()
{
#if defined(XR_PLATFORM_APPLE)
    if (g_nsEventInputCvar)
        return;
#endif
    // ... existing SDL_PeepEvents loop
}
```

- [ ] **Step 4: Capture-mode bridge — `CInput` уведомляет shim**

Найти где `CInput` вызывает `SDL_SetRelativeMouseMode` (capture toggle). Грепнуть `SetRelativeMouseMode` в xr_input.cpp. Возможно в `SetMouseCapture` или похожей функции.

После каждого вызова `SDL_SetRelativeMouseMode(SDL_TRUE/FALSE)` добавить:

```cpp
#if defined(XR_PLATFORM_APPLE)
OpenXRay_SetMouseCaptureMode(<captured ? 1 : 0>);
#endif
```

- [ ] **Step 5: Build (SERIAL)**

```bash
make build-release
```

Expected: exit 0.

### Task 3.2: A.3.3 smoke + commit

- [ ] **Step 1: Ship + запуск**

```bash
make ship
open /Applications/OpenXRay-Dev.app
```

- [ ] **Step 2: Mouse smoke**

New Game → Cordon. Проверить:
- Look around: оба axis работают, sensitivity ощущается как раньше
- Левый клик стрельба
- Правый клик прицел
- Колесо (или trackpad-scroll вверх/вниз) — переключение оружия
- Esc → меню → курсор виден; навигация по кнопкам hover-style
- В меню кликнуть на кнопку → срабатывает

- [ ] **Step 3: Retina проверка**

В меню курсор должен попадать в кнопки точно. Если в координатах сбой (курсор сдвинут на половину) — `backingScaleFactor` mismatch, debug.

- [ ] **Step 4: Commit A.3.3**

```bash
git add src/xrEngine/xr_input.cpp
git commit -m "xrEngine: подключить NSEvent mouse + scroll к IR_On* callbacks (#120 A.3 step 3/4)

Расширяет CInput::NSEventDrain мышь и скролл к существующим
IR_OnMouseMove/Press/Release/Wheel callbacks. Capture-mode bridge
через OpenXRay_SetMouseCaptureMode синхронизирует shim с CInput при
SDL_SetRelativeMouseMode toggle.

Non-captured mode даёт абсолютные pixel coords (через
locationInWindow × backingScaleFactor, с flip Y из Cocoa bottom-left
в engine top-left). Captured mode даёт deltas из NSEvent.deltaX/Y.

SDL MouseUpdate под Apple-gate'ом становится no-op — mouse events
consumed handler'ом раньше.

Step 3 of 4 toward issue #120: A.3 NSEvent input pipeline.
Spec: docs/superpowers/specs/2026-05-26-a3-nsevent-input-design.md
Plan: docs/superpowers/plans/2026-05-26-a3-nsevent-input.md"
```

---

## Phase 4: A.3.4 — Focus/sleep recovery + 30-min soak

**Owner:** apple-platform agent.

**Smoke gate:** Cmd-Tab во время удержания W → возврат → нет stuck; sleep → wake → нет stuck; 30 min CoP gameplay clean.

### Task 4.1: Расширить `OpenXRayCocoaShim` — focus/sleep/backing observers

**Files:**
- Modify: `src/xrEngine/macos_cocoa_shim.mm`

- [ ] **Step 1: Прочитать существующие observer'ы в shim'е**

Read existing `applicationDidBecomeActive:` / `applicationWillResignActive:` (~line 143-165) и `workspaceWillSleep:` / `workspaceDidWake:` (~244-275).

- [ ] **Step 2: В `applicationWillResignActive:` вызвать synthetic release**

После existing forward к SDL delegate:

```objc
// Synthetic release всех зажатых клавиш — избегает stuck-keys когда
// фокус потерян (KeyUp может уйти другому app)
extern "C" void OpenXRay_SyntheticReleaseAllKeys(void);
OpenXRay_SyntheticReleaseAllKeys();
```

- [ ] **Step 3: В `applicationDidBecomeActive:` — синхронизация modifier state**

После existing forward:

```objc
// Сбросить g_lastModifierFlags к текущему реальному состоянию модификаторов
// (могло измениться пока мы были не в фокусе)
g_lastModifierFlags = (uint32_t)([NSEvent modifierFlags] & NSEventModifierFlagDeviceIndependentFlagsMask);
```

- [ ] **Step 4: В `workspaceWillSleep:` вызвать synthetic release**

Аналогично applicationWillResignActive.

- [ ] **Step 5: Добавить observer для `windowDidChangeBackingProperties:`**

В существующем `OpenXRay_ArmLifecycleObservers()` или там же где другие observers:

```objc
[[NSNotificationCenter defaultCenter] addObserver:shim
    selector:@selector(windowDidChangeBackingProperties:)
    name:NSWindowDidChangeBackingPropertiesNotification
    object:nil];
```

Метод в `OpenXRayCocoaShim`:

```objc
- (void)windowDidChangeBackingProperties:(NSNotification*)notification
{
    NSWindow* window = (NSWindow*)[notification object];
    if (window)
    {
        g_backingScaleFactor = (float)[window backingScaleFactor];
        Msg("OpenXRay: backing scale factor changed to %.2f", g_backingScaleFactor);
    }
}
```

- [ ] **Step 6: Build (SERIAL)**

```bash
make build-release
```

Expected: exit 0.

### Task 4.2: Full smoke + 30-min soak + commit

- [ ] **Step 1: Ship + запуск**

```bash
make ship
open /Applications/OpenXRay-Dev.app
```

- [ ] **Step 2: Stuck-key recovery test — Cmd-Tab**

New Game → Cordon. Зажать W (персонаж бежит). Cmd-Tab → переключиться в Safari. Подождать 5 секунд. Cmd-Tab обратно. Expected: персонаж стоит (synthetic release сработал).

- [ ] **Step 3: Stuck-key recovery test — sleep**

Запустить заново. New Game. Зажать W. Закрыть крышку MacBook → подождать пока экран погаснет → открыть. Expected: персонаж стоит после wake.

- [ ] **Step 4: Полный gate checklist из spec'а § 7.2**

Пройти каждый пункт checklist'а:
- [ ] WASD US раскладка
- [ ] WASD RU раскладка
- [ ] Shift+W бег
- [ ] Esc меню
- [ ] Q/E lean
- [ ] 1-6 weapons
- [ ] Tab/F/M/J actions
- [ ] CapsLock toggle
- [ ] Mouse captured: look around
- [ ] Mouse не captured: меню курсор Retina
- [ ] Лев/прав клики
- [ ] Scroll переключение оружия
- [ ] Cmd-Tab → no stuck
- [ ] Sleep → wake → no stuck
- [ ] Cmd+Q graceful
- [ ] nsevent_input 0/1 flip → no stuck
- [ ] Save → quit → relaunch → load round-trip

- [ ] **Step 5: 30-min CoP soak**

Непрерывный gameplay 30 минут на M1. Активный геймплей (не AFK): хождение, стрельба, ESC/menu, save, load.

Что отслеживать:
- No crash, no hang
- No stuck keys after activity
- `~/.openxray-data/logs/openxray_ragnar.log` без новых ERROR/WARN относительно pre-A.3 baseline

Если зависнет: `make sample-hang` сразу.

- [ ] **Step 6: Commit A.3.4**

```bash
git add src/xrEngine/macos_cocoa_shim.mm
git commit -m "xrEngine: focus/sleep recovery + backing scale tracking для NSEvent input (#120 A.3 step 4/4)

Подключает stuck-key recovery: applicationWillResignActive,
workspaceWillSleep дёргают OpenXRay_SyntheticReleaseAllKeys.
applicationDidBecomeActive синхронизирует g_lastModifierFlags
с реальным состоянием [NSEvent modifierFlags].

Добавляет windowDidChangeBackingProperties observer чтобы кэш
backingScaleFactor оставался свежим при переезде окна между
мониторами с разным scale (1x/2x).

Step 4 of 4 toward issue #120: A.3 NSEvent input pipeline.
Closes #120.

Spec: docs/superpowers/specs/2026-05-26-a3-nsevent-input-design.md
Plan: docs/superpowers/plans/2026-05-26-a3-nsevent-input.md"
```

---

## Phase 5: Merge ceremony

Этот phase — foreground claude (не agent).

### Task 5.1: Push + PR + merge

- [ ] **Step 1: Push branch**

```bash
git push -u origin issue-120-a3-nsevent-input
```

- [ ] **Step 2: Создать PR в gitea**

Use `mcp__gitea__pull_request_write` (POST):

Title: `A.3: NSEvent input pipeline (closes #120)`

Body:

```markdown
## Summary

- Заменяет SDL_PollEvent на NSEvent local monitor для keyboard/mouse/scroll/modifiers на macOS
- Controller (gamepad) и window events продолжают идти через SDL
- Cvar `nsevent_input` (default 1) даёт runtime rollback на SDL pipeline
- Stuck-key recovery при focus loss / sleep / cvar flip

## Files

- `src/xrEngine/macos_cocoa_shim.h` — `OpenXRayNSEventRecord` POD + 5 C entries
- `src/xrEngine/macos_cocoa_shim.mm` — ring queue, extended local monitor, focus/sleep/backing observers
- `src/xrEngine/xr_input.{h,cpp}` — `kNSKeyCodeToSDLScancode` table, `NSEventDrain`, Apple-gate early-returns, synthetic release, dev verify command
- `src/Layers/xrRender/xrRender_console.cpp` — cvar `nsevent_input` под Apple-gate

## Gate verification

См. `docs/superpowers/specs/2026-05-26-a3-nsevent-input-design.md` § 7.2.

30-min CoP soak on M1: clean. Cmd-Tab recovery: works. Sleep recovery:
works. RU layout: WASD по физическому keyCode'у — независимо от
раскладки.

## Связано

- Spec: `docs/superpowers/specs/2026-05-26-a3-nsevent-input-design.md`
- Plan: `docs/superpowers/plans/2026-05-26-a3-nsevent-input.md`
- Roadmap: `docs/superpowers/specs/2026-05-25-native-shell-roadmap.md` § 3
- Эпик: #87
- A.1: #115
- A.2: #118
- A.2.1 follow-up: #119 (отдельный, не блокировал A.3)
```

Base: `macos/blackden/master`. Head: `issue-120-a3-nsevent-input`.

- [ ] **Step 3: Self-merge (personal fork, no review nazi)**

После создания PR — merge через gitea UI или MCP `mcp__gitea__pull_request_write` (merge action).

- [ ] **Step 4: Закрыть issue**

Auto-closes by PR body `Closes #120`.

### Task 5.2: Codify findings (memory + engine-map + notes)

- [ ] **Step 1: Update `notes/reference/engine-map.md`**

Добавить в раздел `## Input / keybinds` (line ~523):

```markdown
- **macOS NSEvent pipeline** (A.3, PR #<PR_NUM>):
  - Static keyCode→scancode table: `src/xrEngine/xr_input.cpp` (anon namespace под XR_PLATFORM_APPLE)
  - Local monitor handler + ring queue: `src/xrEngine/macos_cocoa_shim.mm` `OpenXRay_InstallNSEventMonitor()`
  - Drain в OnFrame: `CInput::NSEventDrain()`
  - Cvar runtime rollback: `nsevent_input 0` → SDL pipeline
  - Synthetic release on focus loss / sleep / cvar flip: `OpenXRay_SyntheticReleaseAllKeys()`
- Dev verify command: `verify_input_table` (требует `dev_tools 1`)
```

- [ ] **Step 2: Update `notes/decisions/hang-taxonomy.md`**

Family 1.A status: A.3 не закрывает остаточный класс (e.g. sendto blocking), но keyboard/mouse pump больше не зависит от SDL_PollEvent. Документировать что A.3 уменьшил attack surface для blocking-I/O на main thread.

- [ ] **Step 3: Запустить `remember` skill для session memory**

```
/remember
```

(Skill сам решит что записать.)

- [ ] **Step 4: Commit notes**

```bash
git add notes/reference/engine-map.md notes/decisions/hang-taxonomy.md
git commit -m "notes: codify A.3 — NSEvent input pipeline + Family 1 update"
git push
```

---

## Out-of-scope reminder (из spec'а § 8)

Эти пункты **не делаются в A.3**, но фиксируются как follow-ups:

- Controller / gamepad через NSEvent — отдельный шаг (A.5 audio audit или позже)
- IME / dead-key composition в консоли — отдельный issue если станет блокером
- Trackpad pinch / swipe gestures — движок не использует
- UI hint strings («Стрельба: Ф») — UX-фича
- A.2-rest (NSRunLoop + CADisplayLink) — отдельный шаг логически между A.3 и A.7
- Tablet / Pencil input — out of scope

---

## Self-review результаты

Спецификация § 1-9 покрыта тасками:
- §2 архитектура → Task 2.2 (queue, handler) + Task 2.3 (drain)
- §3.1 monitor extension → Task 2.2
- §3.2 ring queue → Task 2.2
- §3.3 C entries → Task 2.1 + Task 2.2
- §3.4 scancode table → Task 1.1
- §3.5 FlagsChanged diff → Task 2.3 Step 5
- §3.6 mouse two modes → Task 2.2 Step 3 + Task 3.1
- §3.7 scroll → Task 2.2 Step 3 + Task 3.1
- §5.1 stuck-key (focus loss) → Task 4.1
- §5.2 auto-repeat → Task 2.3 Step 5 (isARepeat skip)
- §5.3 CapsLock → Task 2.3 Step 5
- §5.7 queue overflow → Task 2.2 Step 2 (QueuePush warning)
- §5.8 cvar flip → Task 2.4
- §6 sub-steps order → Phase 1-4
- §7 smoke план → Task 2.5, 3.2, 4.2

Type consistency: `OpenXRayNSEventRecord` (Task 2.1) и его поля используются в Task 2.2 (записи), Task 2.3 (drain читает), Task 3.1 (mouse/scroll). `OpenXRay_SyntheticReleaseAllKeys` определена в Task 2.3 Step 4, вызывается в Task 2.3 Step 9 (cvar callback) и Task 4.1 Step 2 (focus loss).

Placeholder scan: `120` — единственный placeholder, заполняется в Phase 0 Task 0.1 Step 2. `<PR_NUM>` в Task 5.2 — заполняется после Phase 5 Task 5.1.

Risk concentration: Task 2.2 (handler + queue + observers) — самый big-bang шаг. Если что-то ломается, rollback через `git revert` целого A.3.2 коммита.
