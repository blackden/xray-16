# Hang taxonomy — OpenXRay (macOS fork)

Объединяющий взгляд на все «hard freeze / unkillable process / system
reboot» инциденты, накопленные в этом форке с 2026-05-13 по 2026-05-25.

Цель: при следующем hang report'е НЕ изобретать таксономию заново и НЕ
плодить N-ную гипотезу под уже известный класс. **Hangs распадаются в 3
distinct root causes**, не дюжину.

Связано: [`decisions.md`](decisions.md),
[`../playbooks/process-supervision.md`](../playbooks/process-supervision.md)
(оригинальный supervisor analysis, частично superseded этим документом),
[`../playbooks/gotchas.md`](../playbooks/gotchas.md) (`Process` section
указывает сюда).

## Context

Сформировано после 2026-05-24 PR #76 smoke (issue #75, SIGUSR1 watchdog
wake): smoke триггерил настоящий main-thread stall через
`check_for_updates` против nc-stall на 127.0.0.1:8000, hard freeze
требовал system reboot, watchdog beacon в логе не успел появиться (или
reboot до 30s, или kernel-level freeze глубже чем `write()` syscall —
неразличимо без kernel logs). PR #76 re-parked.

При разборе постмортема выяснилось что мы не первый раз тут — уже #63,
#71, #49, #52 и ряд GPU TX edge cases описывают **тот же узкий набор
паттернов**. Team-lead дал adversarial review гипотезы «всё это —
missing Windows primitive» (rejected) и моей «macOS less tolerant»
(amplifier, не root cause). Реальный root cause Class A — **engine
делает blocking I/O в main loop без cooperative yield к event loop**;
платформа определяет blast radius, не root cause.

## Три distinct root causes

| Class | Root cause | Известные примеры | Текущий статус |
|---|---|---|---|
| **A. Blocking I/O on main thread** | sync syscall в main loop без cooperative yield к event loop | #71 (ghttp `sendto` block), PR #76 reproducer (`ghttpThink()` poll), GPU TX edge cases на max settings (compressed 3D textures, MSAA, blocking occlusion) | Partial mitigation: #74 pre-flight TCP probe для updater. **Structural fix** = #72 async updater (backlog). Render side: устранение TX-провокаторов идёт постепенно (см. process-supervision.md) |
| **B. Lifecycle/destruction order** | `spatial_unregister` на dead mutex, destructor cascade race, GL teardown с обращением к уже удалённым ref-counted ресурсам | #49 (Cmd+Q ∞-hang), #52 (post-teardown wedge) | **Both FIXED** через destruction-order guards (`g_bStaticDestruction` flag, `reclaim<T>` helper, `DrainEngineRefs()`). Возвращаемся сюда только при regression |
| **C. macOS Force Quit + GPU/IOKit hold = Apple-side bug** | `STAT=TX` — process в uninterruptible kernel wait, держит GPU/IOKit resources; Force Quit signal queued kernel'ом до завершения in-flight syscall (может не наступить никогда) | #63 (Dock Force Quit на max settings) | **Не fixable нашим кодом** — Apple-side. PR #76 был попыткой mitigate **self-inflicted** instance (наш watchdog `_exit` на mid-syscall main → создаёт ту же TX state), не #63. Для пользователя — Shift+Cmd+Q logout или hard reboot |

## Unifying insight — Windows vs macOS WM tolerance

Для Class A root cause везде один — blocking I/O в main thread без
cooperative yield. Blast radius определяется window-manager политикой:

- **Windows**: DWM композит «(Not Responding)» grey overlay над замёрзшим
  окном через ~5 секунд. Остальная ОС работает, dock равен, GPU TDR
  (Timeout Detection Recovery) preempt'ит через 2-5s. User видит
  «одно окно повисло».
- **macOS**: WindowServer ждёт IOSurface updates от приложения, и когда
  main thread block + GPU context hold — весь WM встаёт колом.
  Dock/Spaces/Mission Control unresponsive. User видит system freeze.

Поэтому за годы vanilla GSC никто из Windows-base playerов не репортил
Class A bugs — они там тривиально tolerated. На macOS они = system
reboot. Это объясняет почему #71, #76 reproducer, #63 феноменологически
выглядят одинаково (всё «компьютер замер») но root causes у них разные.

## How to use

При любом новом hang report:

**Шаг 1 — классифицируй по симптому.**

| Симптом | Class |
|---|---|
| Hard system freeze, main loop был занят чем-то sync (network, render, file I/O) | **A** |
| Crash / wedge на shutdown, Cmd+Q, level transition, или teardown | **B** |
| Process в `STAT=TX` (видно через `ps aux \| grep -E "TX"`), Force Quit не работает | **C** |

**Шаг 2 — НЕ изобретай новую гипотезу под этот конкретный случай.**
Taxonomy уже покрывает большую часть. Если симптом не вписывается ни в
один — это сигнал что таксономия неполная (открывай issue с counter-example,
не натягивай её насильно).

**Шаг 3 — implications:**

- **Class A** → ищи async refactor / cooperative yield. Симптоматический
  workaround (timeout, watchdog wake, retry) можно park, но в issue body
  явно указать «workaround, не structural fix» — иначе через год найдём
  Nth workaround в том же месте.
- **Class B** → проверь destruction order через `cpp-engineer` /
  `cpp-analyst` agent. Чаще всего — какая-то RAII ссылка возвращается
  к ресурсу который уже dropped, или mutex destructed до того как его
  acquired thread'ы вышли. См. memory `feedback_delegate_cpp_analysis`.
- **Class C** → document & accept, у нас нет инструментов. Иногда
  self-inflicted instance можно mitigate (как пытался PR #76 со своим
  watchdog `_exit`), но Apple-side bug — нет.

## What's NOT in scope

Этот документ — taxonomy + decision framework, **не план implementation**.
Структурные fix'ы каждого класса:

- **Class A** — #72 (async updater), будущий «AppKit run loop как single
  source of truth» (часть native rewrite, см. #87), main-thread stall
  detector instrumentation (team-lead suggestion 2026-05-25).
- **Class B** — уже структурно решено для известных instances; новые
  обнаружатся как regression если будут.
- **Class C** — никакого. Wait for Apple, или избегай triggering
  conditions.

Каждый — отдельный epic. Ссылки в backlog (gitea).

## Cross-references

- [issue #75 postmortem](https://git.fedorov.tech/ragnar/xray-16/issues/75) —
  PR #76 inconclusive smoke 2026-05-24
- [issue #87 handoff](https://git.fedorov.tech/ragnar/xray-16/issues/87) —
  native rewrite brainstorming, hang taxonomy упомянут как input
- [`../playbooks/process-supervision.md`](../playbooks/process-supervision.md) —
  оригинальная supervisor analysis (2026-05-18). Сейчас частично
  superseded: тогда фокус был «как из userspace убить TX state»
  (ответ: никак), а текущая таксономия признаёт что Class C
  фундаментально не fixable нашим кодом и переключает фокус на Class A
  как highest-ROI работу.
