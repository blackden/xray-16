# Hang taxonomy — OpenXRay (macOS fork)

Объединяющий взгляд на все «hard freeze / unkillable process / system
reboot» инциденты, накопленные в этом форке с 2026-05-13.

Цель: при следующем hang report'е НЕ изобретать таксономию заново и НЕ
плодить N-ную гипотезу под уже известный класс. **Hangs у нас падают
в две root-cause family** — не дюжину, и не три, как утверждала первая
версия этого документа (см. Amendment 2026-05-25 ниже).

Связано: [`decisions.md`](decisions.md),
[`../playbooks/process-supervision.md`](../playbooks/process-supervision.md)
(оригинальный supervisor analysis, partially superseded этим
документом), [`../playbooks/gotchas.md`](../playbooks/gotchas.md)
(`Process` section указывает сюда).

## Context

Сформировано после 2026-05-24 PR #76 smoke (issue #75, SIGUSR1
watchdog wake): smoke триггерил настоящий main-thread stall через
`check_for_updates` против nc-stall на 127.0.0.1:8000, hard freeze
требовал system reboot. PR #76 re-parked. При разборе постмортема
выяснилось что мы не первый раз тут — уже #63, #71, #49, #52 и ряд
GPU TX edge cases описывают **тот же узкий набор паттернов**.

**Amendment 2026-05-25** — пушбэк от ragnar'а: «"OS bug" меня не
устраивает — наше приложение должно подчиняться системе несмотря на её
"баги"». Adversarial review через team-lead подтвердил: первая версия
документа разделяла на три distinct root causes (A/B/C), но реально это
**две family**: A и C — surface manifestations одной и той же
architectural violation, B — отдельный beast (lifecycle/destruction
order). См. issue #95.

## Две root-cause family

### Family 1 — Main-thread cooperative-contract violation

**Root cause:** main thread у нас одновременно гонит render, update,
network polling, file I/O **и** SDL event pump (на macOS
`SDL_PumpEvents` синхронно дёргает `[NSApp nextEventMatchingMask:]`
каждый кадр). NSRunLoop крутится **только пока main thread жив и
пампит**. Любой blocking syscall в main без cooperative yield → SDL
не пампит → AppKit не сервится → WindowServer не получает IOSurface
update от нас → Force Quit signal приходит но handler в runloop не
вызывается → GPU/IOKit context висит → system-wide freeze.

Симптомы (failure surfaces одного и того же root cause):

| Surface | Где конкретно залип main | Известные примеры | Текущий статус |
|---|---|---|---|
| **1.A — blocking I/O** | sync syscall в main loop (sendto, ghttpThink poll, file I/O без async) | #71 (ghttp `sendto`), PR #76 reproducer (`ghttpThink()` poll) | **Closed / mitigated** (#117 A.2, commit `9f2232d21` + teardown safety): ghttp polling вынесен на serial dispatch_queue worker, completion records маршалятся на main через atomic-flag drain. #74 pre-flight probe retained как UX (быстрый «нет соединения», 200ms). Residual: shutdown-path DNS-stall edge case still relies on 10s watchdog (#75) — A.2.1 follow-up: patch vendored ghttp `gethostbyname` → `getaddrinfo` with bounded timeout. |
| **1.C — GPU/IOKit hold** | GPU command wait, blocking occlusion query, driver-side stall (compressed 3D textures, MSAA на edge configs) | #63 (Dock Force Quit на max settings), GPU TX от render edge cases | Symptom mitigation идёт постепенно (см. process-supervision.md); structural fix общий с 1.A |

**Это НЕ Apple-side bug.** STAT=TX это process в uninterruptible
kernel wait потому что **мы** держим IOKit handle и не отпускаем —
вина наша, не Apple. Хорошо ведущее себя macOS-приложение Force
Quit'ится потому что (a) держит main thread responsive,
(b) кооперирует с runloop, (c) отпускает GPU context по
`NSApplicationWillTerminate` через cleanup hook. Мы не делаем ни
(a), ни (b), ни (c).

### Family 2 — Static destruction ordering

**Root cause:** undefined destruction order между global/static
объектами — особенно когда GL ref-counted ресурс ссылается на mutex
который уже destructed, или когда `~CGamePersistent` тащит
`spatial_unregister` через dead `ISpatial_DB`. Это происходит **после
exit'а main loop'а** — SDL pump уже не работает, AppKit и не должен
сервиться, Force Quit там как раз работает (process уже за graceful
path).

| Surface | Где залипает | Известные примеры | Текущий статус |
|---|---|---|---|
| **2.B — destructor cascade** | `~CApplication` пытается `Device.Destroy()` после уничтожения зависимых объектов; `spatial_unregister` на dead mutex | #49 (Cmd+Q ∞-hang), #52 (post-teardown wedge) | **Both FIXED** через destruction-order guards (`g_bStaticDestruction`, `reclaim<T>`, `DrainEngineRefs()`) |

**Важно:** Family 2 — **не runloop проблема**. Никакой async refactor /
NSRunLoop integration / worker isolation это не лечит. Fix всегда
через C++ lifecycle audit. Лумпить B в унификацию с A+C = overcorrection
(landmine, в которую team-lead review нас и не дал упасть). См.
landmine #4 в team-lead charter.

## Почему на Windows этого не видно

Для Family 1 это критично: blast radius определяется WM политикой.

- **Windows DWM**: композит «(Not Responding)» grey overlay над
  замёрзшим окном через ~5 секунд. Остальная ОС работает, dock равен,
  GPU TDR preempt'ит через 2-5s. User видит «одно окно повисло».
- **macOS WindowServer**: ждёт IOSurface updates от приложения, и
  когда main thread block + GPU context hold — весь WM встаёт колом.
  Dock/Spaces/Mission Control unresponsive. User видит system freeze.

Vanilla GSC engine был написан под Windows DWM tolerance — никто из
Windows-base playerов годами не репортил Family 1 bugs, они там
тривиально tolerated. На macOS они = system reboot. Это объясняет
почему #71, #76 reproducer, #63 феноменологически выглядят одинаково
(всё «компьютер замер») — root cause у них общий (Family 1), а
выглядят похоже потому что macOS не толерирует violation cooperative
contract'а.

Family 2 одинаково ломается на любой OS — это чистый C++ UB на
destruction order, без OS-зависимости.

## How to use

При любом новом hang report:

**Шаг 1 — классифицируй по симптому.**

| Симптом | Family / Surface |
|---|---|
| Hard system freeze, main loop был занят чем-то sync (network, render, file I/O) | **1.A** |
| Process в `STAT=TX` (видно через `ps aux \| grep -E "TX"`), Force Quit не работает, max graphics settings | **1.C** |
| Crash / wedge на shutdown, Cmd+Q, level transition, или teardown — main loop уже вышел | **2.B** |

**Шаг 2 — НЕ изобретай новую гипотезу под этот конкретный случай.**
Family 1 уже покрывает большую часть системных freeze'ов; Family 2
закрыт известными fix'ами и проявится только как regression. Если
симптом не вписывается ни в одну — это сигнал что таксономия неполная
(открывай issue с counter-example, не натягивай её насильно).

**Шаг 3 — implications:**

- **Family 1 (Surface 1.A или 1.C)** → ищи async refactor / cooperative
  yield / worker thread isolation. Симптоматический workaround
  (timeout, watchdog wake, retry) можно park, но в issue body явно
  указать «workaround, не structural fix» — иначе через год найдём
  Nth workaround в том же месте. Structural fix общий — см. ниже.
- **Family 2 (Surface 2.B)** → проверь destruction order через
  `cpp-engineer` / `cpp-analyst` agent. Чаще всего — какая-то RAII
  ссылка возвращается к ресурсу который уже dropped, или mutex
  destructed до того как acquired thread'ы вышли. См. memory
  `feedback_delegate_cpp_analysis`.

## Structural fix direction (Family 1 only)

Family 1 имеет один структурный fix: **NSRunLoop как single source of
truth для main loop**, всё блокирующее (network, file I/O, shader
compile, async asset load) выносится в worker threads / dispatch
queues / async ghttp callback chain. Main thread занимается **только**
рендером и event dispatch'ем; runloop крутится непрерывно и
кооперирует с AppKit/WindowServer.

Это совпадает с goals issue **#87 (native rewrite brainstorming)** и
даёт ему concrete technical justification поверх «нативный UX
красивее». До #87 — incremental fixes того же направления:

- **#72** — async updater (Family 1.A specifically). **Subsumed** в #117 A.2
  (ghttp dispatch_queue worker — структурно решает 1.A для всех ghttp poll
  paths, не только updater).
- **#74** — pre-flight TCP probe (workaround для 1.A, не structural). Retained
  как UX-only (200ms feedback) после A.2.
- **#117 A.2** — ghttp/gsCore polling offload (closed 2026-05-26, PR на
  `macos/blackden/master`).
- **#120 A.3** — NSEvent input pipeline (closed 2026-05-27, PR #121).
  Keyboard/mouse/scroll/modifier flags больше не зависят от SDL_PollEvent
  на macOS; SDL pump для них становится no-op. Это не закрывает
  Family 1.A напрямую (input не был blocking I/O), но **уменьшает
  attack surface**: меньше путей через `[NSApp nextEventMatchingMask]`
  → меньше места где AppKit может зависнуть на main thread.
- Main-thread stall detector instrumentation (team-lead suggestion
  2026-05-25, ещё не открыт issue)

Family 2 не требует structural fix — лечится точечно через
destruction-order guards и уже закрыта для известных instances.

## What's NOT in scope

Этот документ — taxonomy + decision framework, **не план
implementation**. Структурные fix'ы каждой family:

- **Family 1** — #72, AppKit run loop unification (часть #87), main-
  thread stall detector instrumentation (team-lead suggestion).
- **Family 2** — уже структурно решено для известных instances; новые
  обнаружатся как regression если будут.

Каждый — отдельный epic. Ссылки в backlog (gitea).

## Cross-references

- [issue #75 postmortem](https://git.fedorov.tech/ragnar/xray-16/issues/75) —
  PR #76 inconclusive smoke 2026-05-24 (пример Family 1.A)
- [issue #87 handoff](https://git.fedorov.tech/ragnar/xray-16/issues/87) —
  native rewrite brainstorming, получает technical justification из
  этого split
- [issue #95](https://git.fedorov.tech/ragnar/xray-16/issues/95) —
  adversarial review 2026-05-25 который привёл к этому patch'у
- [`../playbooks/process-supervision.md`](../playbooks/process-supervision.md) —
  оригинальная supervisor analysis (2026-05-18). Сейчас partially
  superseded: тогда фокус был «как из userspace убить TX state»
  (ответ: никак), а текущая таксономия признаёт что Family 1 имеет
  structural fix path (NSRunLoop integration + worker isolation), а
  supervisor подход — workaround для Family 1 без structural fix.
