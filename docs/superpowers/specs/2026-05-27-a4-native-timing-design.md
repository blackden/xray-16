# A.4 — Native timing (design spec)

Дата: 2026-05-27
Issue: [#123](https://git.fedorov.tech/ragnar/xray-16/issues/123)
Эпик: [#87](https://git.fedorov.tech/ragnar/xray-16/issues/87)
Roadmap row: §3 A.4 в `docs/superpowers/specs/2026-05-25-native-shell-roadmap.md`

## 1. Задача

Заменить `SDL_GetPerformanceCounter` / `SDL_GetPerformanceFrequency` /
`SDL_GetTicks` в `src/xrCore/_math.cpp` (единственная hot-path точка
SDL timing API в нашем macOS-бандле) на Apple-native (`mach_absolute_time`
+ `mach_timebase_info`). Замена под `#if defined(XR_PLATFORM_APPLE)`, на
других платформах остаётся SDL.

Это шаг A.4 из roadmap native macOS shell (после A.1/A.2/A.3).

## 2. Premise audit (2026-05-27)

### SDL timing surface

`grep SDL_Get{Ticks,PerformanceCounter,PerformanceFrequency}\|SDL_Delay\|SDL_TICKS_PASSED\|SDL_AddTimer\|SDL_RemoveTimer`
по `src/` — единственный hot-path call site:

| Файл:line | Call | Caller |
|---|---|---|
| `src/xrCore/_math.cpp:22` | `SDL_GetPerformanceFrequency()` | init `qpc_freq` (single-shot) |
| `src/xrCore/_math.cpp:28` | `SDL_GetPerformanceCounter()` | `CPU::QPC()` (hot) |
| `src/xrCore/_math.cpp:35` | `SDL_GetTicks()` | `CPU::GetTicks()` |

Вне macOS hot path:
- `src/utils/mp_gpprof_server/...` — MP profiler, не собирается в наш бандл
- `src/xrNetServer/empty/NET_Client.cpp` — MP empty stub
- `src/utils/.../gsPlatformUtil.c` — GameSpy shim (наш после A.2, fallback)

`SDL_Delay` / `SDL_AddTimer` / `SDL_RemoveTimer` / `SDL_TICKS_PASSED` —
**не используются** в engine.

### `CTimer` / `CStatTimer` family

Уже на `std::chrono::high_resolution_clock` (`src/xrCore/FTimer.h:32`).
**Не входит в scope A.4** — это отдельная timing-система, native по
определению.

### Callers `CPU::QPC()` и `CPU::GetTicks()`

| Caller | Класс использования |
|---|---|
| `xrGame/{alife,stalker,sound_player,secure_messaging}*` (11 sites) | RNG seed (one-shot) |
| `Layers/xrRenderDX11/dx11SH_RT.cpp:23,335` | Unique `_order` для RT sort |
| `Layers/xrRenderGL/glSH_RT.cpp:25` | Unique `_order` для RT sort |
| `xrCore/Threading/Lock.cpp:39,47` | Spin/wait monotonic timing |
| `xrCore/_math.cpp:88` | Random seed |
| `xrGame/Level_network.cpp:370,378` | MP timeout (relative delta) |
| `xrGame/game_sv_event_queue.cpp:41/81/107/128/178` | MP server timeout (relative delta) |
| `utils/xrCompress/xrCompress.cpp:332,386` | Offline utility timing |

**Wall-clock callers отсутствуют.** Все consumers — либо RNG seed (любой
clock работает), либо relative delta (`Now - StartTime`, работает с
любым monotonic clock).

## 3. API choice

### Что используем

| Замена | Apple-native |
|---|---|
| `SDL_GetPerformanceCounter()` | `mach_absolute_time()` |
| `SDL_GetPerformanceFrequency()` | через `mach_timebase_info` → `1e9 * denom / numer` (ticks-per-second в наших единицах) |
| `SDL_GetTicks()` | `mach_absolute_time()` конвертированный в миллисекунды через ту же timebase |

`mach_timebase_info` инициализируется один раз. На Apple Silicon обычно
`numer=125, denom=3` → 1 tick = ~41.67 ns. На Intel — `numer=1,
denom=1` (already-nanoseconds), но мы не закладываемся на это.

### Почему не `clock_gettime(CLOCK_MONOTONIC_RAW)`

- `mach_absolute_time` — оригинальный Darwin API, никаких syscall'ов
  (внутри VDSO), быстрее
- `CLOCK_MONOTONIC_RAW` на Darwin реализован поверх `mach_absolute_time`,
  лишний слой
- Pattern в Apple sample code (CoreAudio, AVFoundation) — именно
  `mach_absolute_time`

### Почему не `CFAbsoluteTimeGetCurrent`

- Возвращает wall-clock (NSDate epoch), не monotonic — sleep/timezone
  change ломают math
- Двойная точность (`double`) — потеря precision на длинных runtime'ах
- `mach_absolute_time` дешевле и монотонней

### `mach_absolute_time` semantics

- **Монотонный** на uptime
- **НЕ включает suspended time** (за sleep/wake — пауза). Это правильно
  для frame delta math (не хотим delta=8h после wake от ночного sleep)
- Apple-native alias: `mach_continuous_time()` включает suspended time —
  **нам не нужен**

### A.1 interaction check

`OpenXRay_ApplyPendingLifecycleEvent` в `Engine.cpp` (sleep-recovery от
A.1) триггерится observer'ом `NSWorkspaceDidWakeNotification`, **не**
timing-delta detection. Замена timing semantics на «pause во время
sleep» не ломает A.1 — pause render просто продолжает с того же
tick-counter после wake.

## 4. Cvar gate (per team-lead pre-flight review 2026-05-27)

Cvar `native_timing` (mirror A.3 `nsevent_input` pattern):
- Default `1` на macOS
- Runtime toggle для A/B microbench без rebuild
- Soft swap — `CPU::QPC()` смотрит на cvar **каждый вызов** (cheap
  branch); на других платформах cvar отсутствует (compile-time noop)

Без cvar microbench не сможет сравнить SDL vs mach баселины на одном
бинаре.

## 5. Sub-step разбиение

Per team-lead: **один PR, два коммита**:

| # | Commit | Что делает |
|---|---|---|
| A.4.1 | Native impl за ifdef, cvar default `0` | Добавить `_math_native_apple.{h,cpp}` (или ifdef-блок в `_math.cpp`), реализовать `apple_qpc()` / `apple_qpc_freq()` / `apple_get_ticks_ms()`. `CPU::QPC()` и `CPU::GetTicks()` дёргают native только если cvar=1; default=0 → бинарь идентичен предыдущему |
| A.4.2 | Cvar flip default → `1` | Один-строчный flip default value. Позволяет revert flip отдельно если gate провалится |

Каждый коммит — собирается, smoke-runs.

## 6. Edge cases

### Overflow / signedness

`u32 qpc_counter` инкрементируется каждый QPC() — wrap каждые ~2^32
вызовов. **Существующее поведение, out-of-scope A.4** (не трогаем).

`u32 GetTicks()` ms — wrap через ~49.7 дней. Существующее SDL поведение;
MP timeout math (`Level_network.cpp:370-378`) использует unsigned delta,
устойчив к wrap. Native реализация должна возвращать **тот же тип** и
тот же wrap, иначе MP клиент сломается.

### `mach_timebase_info` init thread safety

`_math.cpp:22` инициализирует `qpc_freq` как static (single-thread на
старте процесса). Native impl должен сделать так же: либо
`std::once_flag`, либо `static const` lambda init.

### `mach_absolute_time` resolution

На Apple Silicon — ~41.67 ns. На Intel — 1 ns. Оба выше чем
`SDL_GetPerformanceFrequency` обычно даёт; precision только улучшается.

## 7. Гейт «готово»

Per roadmap §3 A.4 + team-lead дополнения:

- [ ] Microbench: одна сессия `native_timing 0` (SDL baseline), одна `native_timing 1` (native), сравнение `frame_time_ms` median/p99 — идентичны в пределах jitter
- [ ] 1h soak CoP на M1 с `native_timing 1` — нет timing drift, нет regression vs baseline
- [ ] Physics smoke: бросить гранату → рэгдолл, animation curves OK (no twitching)
- [ ] Sleep/wake gameplay test: в игре триггерить sleep (Cmd+Opt+Power → display sleep), wake, продолжить — нет крэша, frame delta sane
- [ ] `make sample-hang` на idle main menu — нет spin loops
- [ ] Save→quit→relaunch→load round-trip clean (SDL и native режимы)
- [ ] Engine log без новых `! timer` / `! frame` warnings
- [ ] `otool -L bin/arm64/ReleaseMasterGold/xr_3da` — SDL2 пока остаётся (выйдет в A.7)

## 8. Dispatch

`apple-platform` agent с briefом:
- Файл `xrCore/_math.cpp` — core engine файл, но тематика чисто Darwin
  clock API
- Continuous vs absolute time clarification (см. §3 здесь)
- A.1 sleep/wake interaction check (см. §3)
- Cvar `native_timing` (mirror A.3 pattern)
- Two-commit PR layout (impl + flip)

## 9. Связано

- Issue: [#123](https://git.fedorov.tech/ragnar/xray-16/issues/123)
- Эпик: [#87](https://git.fedorov.tech/ragnar/xray-16/issues/87)
- Roadmap spec: [`docs/superpowers/specs/2026-05-25-native-shell-roadmap.md`](2026-05-25-native-shell-roadmap.md)
- A.1: [#114](https://git.fedorov.tech/ragnar/xray-16/issues/114) / PR #115
- A.2: [#117](https://git.fedorov.tech/ragnar/xray-16/issues/117) / PR #118
- A.3: [#120](https://git.fedorov.tech/ragnar/xray-16/issues/120) / PR #121
- A.3 spec template: `docs/superpowers/specs/2026-05-26-a3-nsevent-input-design.md`
