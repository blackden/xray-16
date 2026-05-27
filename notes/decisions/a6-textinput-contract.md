# A.6 — Native cleanup + `ITextInputBackend` contract (V4)

**Date:** 2026-05-27
**Roadmap step:** A.6 (native-shell-roadmap §3)
**Issues:** [#124](https://git.fedorov.tech/ragnar/xray-16/issues/124) (regression), [#132](https://git.fedorov.tech/ragnar/xray-16/issues/132) (ring buffer), [#134](https://git.fedorov.tech/ragnar/xray-16/issues/134) (stuck modifier)
**PRs:** #133, #135, #136, #137, #138, #139
**Status:** ✓ Closed. V4 contract принят после adversarial consilium.

## TL;DR

A.6 в роадмапе изначально был «зачистка остатков SDL_* под Apple-gate,
гейт `grep SDL_` пусто». На premise audit'е (team-lead consilium с
apple-platform + cpp-engineer) выяснилось что для text-input части
это формулировка **архитектурно несостоятельна** без unparking A.1
part 2 (engine owns NSWindow + NSView).

**Принятое решение (Variant 4):**

- Engine-owned `ITextInputBackend` контракт в `src/xrEngine/`.
- SDL impl везде (Linux/Windows/macOS) для текущего этапа — wraps
  `SDL_StartTextInput`, `SDL_StopTextInput`, `SDL_IsTextInputActive`.
- Native macOS impl через `NSTextInputClient` на own'ой NSView —
  **отложен до A.1 part 2 unpark**.
- `IR_OnTextInput(pcstr utf8)` сигнатура сохранена — consumer
  refactor не нужен.

**Гейт A.6 пересмотрен:** не «grep `SDL_` под Apple-gate пусто», а
«`SDL_*` calls под Apple-gate routed через engine abstractions
с documented platform reason; `SDL_StartTextInput` survives behind
`ITextInputBackend` до A.1 part 2 unparking».

## Почему V3 («полный native text-input в A.6») unsound

Adversarial review apple-platform agent:

1. **`NSTextInputClient` — view-level протокол.** Без own'ой NSView
   которая является `firstResponder`, IME (CJK, Emoji picker,
   marked text) не триггерится — AppKit не маршрутизирует keyDown
   в IME server без firstResponder. Не «marked text без preedit», а
   **ничего вообще**.
2. **Dead-keys (US-International, Lithuanian, Vietnamese)** требуют
   TSM `interpretKeyEvents:` на NSResponder ИЛИ ручного state machine
   через `UCKeyTranslate` с threading `UInt32 deadKeyState` между
   keystroke'ами.
3. **Все крупные проекты делают одно и то же:** SDL2
   (`SDL_cocoakeyboard.m` → `SDLTranslatorResponder : NSView <NSTextInputClient>`),
   GLFW (`cocoa_window.m` → `GLFWContentView`), Chromium
   (`render_widget_host_view_mac.mm`), Firefox
   (`widget/cocoa/TextInputHandler.mm`). Никаких shortcuts.

Без A.1 part 2 максимум через NSEvent monitor + `[event characters]`:
**Latin + Cyrillic precomposed**. Это **молчаливая регрессия** для
US-Intl / CJK / Emoji-picker пользователей vs SDL'ный status quo.

## Rejected alternative: Option C (subview-NSTextInputClient под SDL'овой window)

Идея: attach engine's own NSView как sibling subview к SDL contentView
(не владея NSWindow), implement NSTextInputClient, switch firstResponder
когда text-input нужен. Гипотеза — это даёт native committed text +
IME + dead-keys **без unparking A.1 part 2**.

**Adversarial audit (apple-platform agent, 2026-05-27): NOT VIABLE.**

Два killshot'а:

1. **SDL обходит firstResponder.** `Cocoa_HandleKeyEvent` в
   `SDL_cocoakeyboard.m` (SDL2 master) вызывает `interpretKeyEvents:`
   на `data.fieldEdit` **по имени**, не на whoever is firstResponder.
   Даже если наш subview выиграет firstResponder, text payload идёт
   на SDL's `SDLTranslatorResponder`. Мы не получаем `insertText:`.

2. **Свой собственный monitor блокирует responder chain.** В
   `src/xrEngine/macos_cocoa_shim.mm:570-578` возврат `nil` на keyDown
   когда `g_nsEventInputEnabled != 0` consume'ит event до того как
   AppKit dispatch'ит в responder chain. Ни SDL, ни наш subview не
   получают `keyDown:`. **Native text-input через subview структурно
   несовместим с A.3 NSEvent monitor.**

Дополнительно: SDL bug #6505 показывает что кто-то уже пытался этим
путём (override `keyDown:` + `interpretKeyEvents:`) и failed.
Cross-platform precedent — нет.

## Почему V4 — не костыль, а легитимная абстракция

Зеркалит A.4 (native timing) — engine-owned контракт с platform impls,
не SDL-shaped wrapper. Разница «костыль vs абстракция»:

- **Костыль:** прячет ошибку / silent failure / makes CI green без
  semantic backing. Контракт = SDL-shape.
- **Легитимная абстракция:** stable internal contract (Begin/End/IsActive)
  с platform-specific implementations. Контракт — наш, SDL — одна из impl.

Когда A.1 part 2 unparks → native macOS impl падает на место без
рефакторинга consumer'ов (drop-in замена backend'а).

## Consumer surface (cpp-engineer findings)

`IR_OnTextInput(pcstr text)` имеет 4 override'а:
- `CConsole`, `CLevel`, `CMainMenu` — pure dispatch glue.
- `ide` (ImGui overlay) — `ImGui::AddInputCharactersUTF8(text)`.

**Реальный consumer text bytes — один widget:**
`line_edit_control::on_text_input` (`src/xrEngine/line_edit_control.cpp:368`)
+ ImGui sink. Ни один consumer не подписан на `SDL_TEXTEDITING`
(marked text). Все хотят committed UTF-8 only.

## Root cause #124 (cpp-engineer findings)

`SDL_PumpEvents` + `SDL_FlushEvents` в `EnableTextInput`/`DisableTextInput`
добавлены 2020-Oct commit `1f7cd9e39` (Xottab-DUTY, «Fixed input lag
on low FPS») — pump promotes queued OS events, flush дропает stale
TEXTEDITING/TEXTINPUT перед toggle.

A.3 NSEvent monitor сломал инвариант: pump теперь re-enters NSApp
queue через monitor block, который swallow'ит keyDown (return nil).

**Root fix (PR #136):**
1. Gate NSEvent monitor на `SDL_IsTextInputActive()` — text input
   active → `return event` (SDL обработает + сгенерит SDL_TEXTINPUT),
   inactive → текущий A.3 path (swallow + queue).
2. Pump removal — hygiene, не load-bearing (cpp-engineer verdict).
   Parked как `XXX A6-PUMP-REMOVAL` в `SDLTextInputBackend.h`.

После PR #136 `nsevent_input=1` возвращён default'ом.

## Что отброшено из spec

- **`sysctlbyname`** — 0 хитов `SDL_GetCPUCount` / `SDL_GetSystemRAM`
  в `src/`. Нет тела работы.
- **NSOpenPanel** — не нашли SDL file-dialog API. Если когда-то
  всплывёт — отдельно.

## Что in-scope A.6 — что было сделано

| # | PR | Файлы | Что |
|---|---|---|---|
| #133 | Ring buffer atomics | `macos_cocoa_shim.mm` | `os_unfair_lock` — prerequisite для `nsevent_input=1` default |
| #135 | Stuck modifier focus loss (#134) | `Engine.cpp`, `macos_cocoa_shim.mm`, `xr_input.cpp` | Deferred IR_ReleaseAll на frame boundary через atomic flag |
| #136 | #124 root fix | `macos_cocoa_shim.mm`, `xr_input.cpp` | Gate monitor на `SDL_IsTextInputActive()` + `nsevent_input=1` default restored |
| #137 | A.6.1 NSPasteboard | `os_clipboard_mac.mm` (new), `os_clipboard.cpp`, `Device_imgui.cpp`, `CMakeLists.txt` | Native clipboard через NSPasteboard |
| #138 | A.6.2 NSAlert | `xrDebug_mac.mm` (new), `xrDebug.cpp`, `CMakeLists.txt` | Native dialogs через NSAlert |
| #139 | A.6.3 ITextInputBackend | `ITextInputBackend.{h,cpp}` (new), `SDLTextInputBackend.{h,cpp}` (new), `xr_input.{h,cpp}` | Engine-owned contract, Option 2 minimal |

## Что отложено

- **Native macOS impl `ITextInputBackend`** через NSTextInputClient
  на own'ой NSView. Привязано к A.1 part 2 unpark.
- **IME / dead-keys support** — открытый вопрос. Текущее SDL поведение
  на macOS их даёт; после native impl нужно решить tier поддержки
  (committed-only / dead-keys / full IME). Decision deferred.
- **`SDL_PumpEvents` removal в Enable/DisableTextInput** — parked как
  `XXX A6-PUMP-REMOVAL` в `SDLTextInputBackend.h`. Hygiene only.

## Новый landmine для playbook

**NSEvent local monitor returning `nil` для keyDown блокирует responder
chain полностью.** Decision point: `src/xrEngine/macos_cocoa_shim.mm`.
Любой будущий план который хочет `[NSResponder keyDown:]` достичь
subview (NSTextInputClient для native text-input, custom hotkey
overlays, accessibility shortcuts) — структурно несовместим с A.3
consume-keyDown pipeline без arbitration flag (= #124 trap class).
Когда A.1 part 2 разморозится — keyDown ownership должен быть в
**одном** месте.

Добавлено в [engine-map.md → Input / keybinds → Apple gotchas](../reference/engine-map.md).

## References

- Roadmap §3: `docs/superpowers/specs/2026-05-25-native-shell-roadmap.md:80-103`
- A.4 precedent (engine-owned contract pattern):
  `docs/superpowers/specs/2026-05-27-a4-native-timing-design.md`
- Apple Cocoa Event Handling Guide — "Handling Key Events"
- Apple TN2128 "Text Input in Carbon"
- SDL2 source: `src/video/cocoa/SDL_cocoakeyboard.m` (reference impl)
- GLFW source: `src/cocoa_window.m` (`GLFWContentView`)
- libsdl-org/SDL #6505 — failed precedent для Option C path

## Принципы, которые сработали

- **Correctness over throughput** ([feedback_correctness_over_throughput]) —
  ушли от Variant 2 (узкий fix с SDL_StartTextInput-шимом как костыль)
  в Variant 4 (структурный engine-owned контракт).
- **Premise audit перед dispatch** ([feedback_premise_audit_roadmap_steps]) —
  team-lead consilium вскрыл что V3 архитектурно несостоятелен,
  до того как код был тронут.
- **Delegate to specialists** ([feedback_delegate_to_specialists]) —
  apple-platform и cpp-engineer отвечали на разные срезы вопроса
  параллельно; foreground синтезировал.
- **No yes-man** ([feedback_no_yes_man]) — пользователь redirected
  с throughput-логики обратно к correctness принципу; foreground
  изменил рекомендацию.

## Постмортем: T1 не закрыл симптом, T1.1+T1.2 закрыли (2026-05-28)

A.6 закрыт **финальным stack'ом 5 PR'ов**, не единичным T1. Первый
закрытый T1 (#142) не починил user-visible симптом несмотря на passing
adversarial review apple-platform + cpp-engineer. Это потребовало 2
follow-up'а:

### T1.1 (#144/#145) — ImGui reactive deadlock

`CConsole::Show()` не вызывает `EnableTextInput` явно. Console — ImGui
based (`InputText` widget), относится на `ide::UpdateTextInput()` (читает
`ImGui::GetIO().WantTextInput`). `WantTextInput` — **реактивный** флаг:
становится `true` ТОЛЬКО после того как `InputText` widget получил первый
keyDown. Pre-T1 это работало пассивно (SDL всегда пампил keyDowns).
Post-T1 gate swallow'ит keyDown до того как SDL/ImGui видит → `InputText`
не активируется → `WantTextInput` навсегда `false` → `EnableTextInput`
никогда не вызывается → deadlock.

**Fix:** `CConsole::Show()` явно вызывает `pInput->EnableTextInput()`,
`Hide()` — `DisableTextInput()`. Mirror'ит `line_edit_control::on_ir_capture/release`
паттерн для legacy CUICustomEdit.

### T1.2 (#146/#147) — SDL drain skip orphaned events

`xr_input.cpp:681-685` (pre-#147) unconditionally skip'ил SDL drain на
Apple когда `nsevent_input=1`. Логика была: «keyDown'ы потребляются
NSEvent monitor'ом перед SDL, drain — no-op». **Это верно только когда
gate=false.** Когда `g_textInputActive=true` (text input active), NSEvent
monitor возвращает event → AppKit forwards → SDL queues `SDL_KEYDOWN` +
`SDL_TEXTINPUT`, но `runSDLDrain=false` → события orphan'ятся в SDL
queue. `IR_OnKeyboardPress(DIK_GRAVE)` не firится → `Hide()` не вызывается
(консоль не закрывается); `SDL_TEXTINPUT` не доходит до ImGui InputText
(не пишет).

**Fix:** убран Apple-conditional skip. Когда gate closed — SDL queue пуст
естественно (monitor swallow'ит), drain автоматически no-op. Когда gate
open — drain критичен. Unconditional drain функционально эквивалентен
«drain when gate open», просто без Apple-branch.

### Уроки для playbook

1. **Adversarial review can miss the obvious if the question framing is wrong.**
   apple-platform и cpp-engineer оба ВИДЕЛИ `runSDLDrain` skip и `WantTextInput`
   реактивность, но не связали их с симптомом потому что мой брифинг
   сфокусировал на гейте, не на orphan'е events post-gate.
2. **2 fail подряд = stop, ask for user symptom details before next fix.**
   Симптом «открывается, не закрывается» от ragnar'а — buzz-killer, дал
   прямой указатель на T1.2 за минуты. Я этого симптома не спрашивал
   между T1 и T1.1 — потратил цикл на blind fix.
3. **ImGui reactive `WantTextInput` это landmine pattern.** Любой gating
   layer на keyDown deadlock'ит ImGui-driven text consumers. Future
   ImGui-driven UI с InputText требует proactive `EnableTextInput` в
   open path.

Codified в memory: `project_sdl_double_gate_macos`,
`project_imgui_wanttextinput_reactive`, `feedback_two_fails_call_team_lead`.

## Финальный stack PR'ов

| PR | Issue | Что закрыл |
|----|-------|------------|
| #142 | #141 | T1: engine-owned `std::atomic<bool> g_textInputActive` flag, NSEvent gate переведён с `SDL_IsTextInputActive()` |
| #145 | #144 | T1.1 hotfix: explicit `EnableTextInput`/`DisableTextInput` в `CConsole::Show/Hide` (ImGui reactive deadlock) |
| #147 | #146 | T1.2 critical: убран Apple-conditional SDL drain skip в `CInput::KeyUpdate` (orphan events когда gate open) |
| #148 | #143 | T2 hygiene: убран `SDL_PumpEvents` из `CSDLTextInputBackend::Start/Stop`, сохранён `SDL_FlushEvent(SDL_TEXTEDITING)` в Stop |
| #150 | #149 | T3 cleanup: удалён dead `IsActive()` surface из `ITextInputBackend` (нет consumers; counter — authoritative) |

A.6 техдолг закрыт. Ready for A.7 (SDL2 removed из macOS-билда).
