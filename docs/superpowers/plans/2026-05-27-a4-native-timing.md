# A.4 — Native timing (implementation plan)

Дата: 2026-05-27
Issue: [#123](https://git.fedorov.tech/ragnar/xray-16/issues/123)
Design spec: [`docs/superpowers/specs/2026-05-27-a4-native-timing-design.md`](../specs/2026-05-27-a4-native-timing-design.md)
Branch: `issue-123-a4-native-timing`
Owner: `apple-platform` agent

## Phase 0 — setup (foreground)

- ✓ Premise audit (см. design spec §2)
- ✓ Team-lead pre-flight review (2026-05-27)
- ✓ Design spec + plan committed
- ✓ Issue #123 opened with gate checklist
- ✓ Branch `issue-123-a4-native-timing` создан off `macos/blackden/master`

## Phase 1 — A.4.1: native impl за ifdef, cvar default 0

Owner: `apple-platform`. Dispatch с briefом ниже.

### Файлы, которые трогаем

- `src/xrCore/_math.cpp` — добавить ifdef-блок с native impl, добавить
  cvar read-path
- `src/xrCore/_math.h` — может быть пустым diff (extern'ы не меняются)
- `src/xrEngine/xr_input_xrkey_to_dik_table.h` либо отдельный новый
  module — выбор apple-platform agent'а как оформить. Если новый
  файл — `src/xrCore/Threading/_math_native_apple.{h,cpp}` или
  `src/xrCore/_math_apple.{h,cpp}` (выбор agent, по стилю проекта)
- Где-то регистрация cvar `native_timing` — паттерн брать с
  `nsevent_input` (см. `src/xrEngine/xr_input.cpp` поиск
  `nsevent_input`)

### Контракт

```cpp
// _math.cpp на Apple
#if defined(XR_PLATFORM_APPLE)
#include <mach/mach_time.h>
extern XRCORE_API int g_native_timing; // cvar, 0/1

namespace
{
    mach_timebase_info_data_t g_timebase;
    bool g_timebase_inited = false;

    void ensure_timebase()
    {
        if (!g_timebase_inited)
        {
            mach_timebase_info(&g_timebase);
            g_timebase_inited = true;
        }
    }

    // mach_absolute_time() * numer / denom = nanoseconds
    // freq в "QPC-единицах" = ticks per second = 1e9 * denom / numer
    u64 apple_qpc_freq()
    {
        ensure_timebase();
        return u64(1'000'000'000ull) * g_timebase.denom / g_timebase.numer;
    }

    u64 apple_qpc() { return mach_absolute_time(); }

    u32 apple_get_ticks_ms()
    {
        ensure_timebase();
        const u64 ns = mach_absolute_time() * g_timebase.numer / g_timebase.denom;
        return u32(ns / 1'000'000ull);
    }
}
#endif

namespace CPU
{
XRCORE_API u64 qpc_freq =
#if defined(XR_PLATFORM_APPLE)
    apple_qpc_freq();  // безусловная замена freq — она читается всеми платформами одинаково
#else
    SDL_GetPerformanceFrequency();
#endif

XRCORE_API u64 QPC() noexcept
{
#if defined(XR_PLATFORM_APPLE)
    u64 _dest = g_native_timing ? apple_qpc() : SDL_GetPerformanceCounter();
#else
    u64 _dest = SDL_GetPerformanceCounter();
#endif
    qpc_counter++;
    return _dest;
}

XRCORE_API u32 GetTicks()
{
#if defined(XR_PLATFORM_APPLE)
    return g_native_timing ? apple_get_ticks_ms() : SDL_GetTicks();
#else
    return SDL_GetTicks();
#endif
}
} // namespace CPU
```

**Важно:** `qpc_freq` на Apple подменяется **безусловно** (не за cvar).
Это значит downstream consumers, которые считают `cycles / qpc_freq =
seconds`, всегда получат корректную секундную интерпретацию для того
QPC которое читают. Если cvar `native_timing=0` и QPC возвращает
`SDL_GetPerformanceCounter()`, а freq у нас apple-based — math
неправильный.

**Решение:** при cvar=0 надо вернуть SDL freq тоже. Альтернатива
аккуратнее:

```cpp
XRCORE_API u64 qpc_freq = SDL_GetPerformanceFrequency();  // default
// На macOS dynamically swap'аем при flip cvar — или
// (проще) фиксируем freq как **одинаковую** в обоих режимах
// через приведение native к SDL-совместимым единицам:
//   apple_qpc() = mach_absolute_time() * numer / denom (наносекунды)
//   apple_qpc_freq() = 1e9 (один наносекунд = одна tick)
// и тогда qpc_freq всегда = 1e9 на Apple
```

Apple-platform agent сам выбирает чище:
- Вариант A: native QPC возвращает **наносекунды**, freq = 1e9 на
  Apple. Cвoп cvar не задевает freq.
- Вариант B: native QPC возвращает raw `mach_absolute_time()`, freq =
  `apple_qpc_freq()`. Cвоп cvar **меняет** freq — сложнее.

Рекомендую **вариант A** (constant freq=1e9, native QPC в нс) — проще,
freq не зависит от cvar, downstream math всегда корректен.

### Cvar регистрация

- Имя: `native_timing`
- Тип: `CCC_Integer`, range `[0..1]`
- Default: `0` (в A.4.1; A.4.2 переключит на `1`)
- Place: там же где `nsevent_input` (apple-platform agent смотрит на
  pattern A.3 и повторяет)

### Verification (commit-local)

```bash
# Сборка
make build-release

# Smoke: бинарь не сломан с cvar default 0 (=identical SDL behavior)
open /Applications/OpenXRay-Dev.app
# CoP main menu, console: `native_timing` → 0
# 5 минут гэймплея — нет regression
# Exit graceful

# A/B microbench
# 1) native_timing 0, 60s в main menu, наблюдаем frame_time_ms median/p99
# 2) native_timing 1, 60s в main menu, идентичные числа в пределах jitter
```

### Definition of done для A.4.1

- [ ] Compile clean: `make build-release`
- [ ] Default cvar=0 → бинарь поведенчески идентичен предыдущему commit'у
- [ ] Cvar=1 в console → switch работает live, нет крэша
- [ ] Microbench A/B: median и p99 frame_time_ms идентичны в пределах ±jitter
- [ ] `xr_3da -nointro` запускается в обоих режимах

## Phase 2 — A.4.2: flip default → 1

Owner: `apple-platform` (один-строчный commit).

### Изменения

- Default cvar `native_timing` → `1`
- Никакой логики — только value change

### Verification (расширенный gate)

Per design spec §7:

- [ ] 1h soak CoP с native default — нет drift, нет regression
- [ ] Physics smoke: бросить гранату, посмотреть рэгдолл, тестовый
      кадр анимации NPC — нет twitching
- [ ] Sleep/wake test: в gameplay триггерить display sleep
      (Cmd+Opt+Power), wake, продолжить — нет крэша, frame delta sane
- [ ] `make sample-hang` на idle main menu — нет spin loops
- [ ] Save → quit → relaunch → load round-trip clean
- [ ] Engine log clean (нет `! timer` / `! frame` warnings)

### Definition of done для A.4.2

Все checkbox'ы выше + user smoke approval.

## Phase 3 — merge ceremony (foreground)

- [ ] Both commits на `issue-123-a4-native-timing`
- [ ] PR в `macos/blackden/master` (title: «xrCore: native timing на macOS (mach_absolute_time, A.4 #123)»)
- [ ] Auto-ship hook отрабатывает, бинарь в `/Applications/OpenXRay-Dev.app` обновлён
- [ ] User smoke 30 min CoP gameplay
- [ ] Merge, close #123
- [ ] Notes commit: добавить landmine в `notes/playbooks/gotchas.md`
      про `mach_absolute_time` vs `mach_continuous_time` semantics
      (для будущих timing changes)
- [ ] Memory update — A.4 done, готов к A.5 (audio audit)

## Apple-platform agent brief (для dispatch'а)

```
Task: implement A.4.1 step of native macOS shell roadmap.

Repo: /Users/ragnar/fedorov_tech/xray-16
Branch: issue-123-a4-native-timing (already created off macos/blackden/master)
Issue: #123
Design spec: docs/superpowers/specs/2026-05-27-a4-native-timing-design.md
This plan: docs/superpowers/plans/2026-05-27-a4-native-timing.md

Implementation scope (Phase 1, A.4.1 only):
- Add Apple-native timing implementation in src/xrCore/_math.cpp under
  #if defined(XR_PLATFORM_APPLE) ifdef
- Use mach_absolute_time + mach_timebase_info (NOT mach_continuous_time —
  we want pause-during-sleep semantics; A.1 sleep-recovery already
  handles wake via NSWorkspace observer)
- Recommended: convert mach_absolute_time to nanoseconds at QPC()
  boundary, set qpc_freq = 1'000'000'000 on Apple (so freq stays constant
  regardless of cvar value)
- Add cvar native_timing (CCC_Integer, range 0..1, default 0 in this
  step) — pattern mirror A.3 nsevent_input (look at how it's registered
  in src/xrEngine/xr_input.cpp or similar)
- CPU::QPC() and CPU::GetTicks() branch on cvar at runtime on Apple,
  unchanged on other platforms
- ONE commit for this phase

Constraints:
- ReleaseMasterGold must build (no exceptions on, no luabind tweaks)
- Style: portable types (u8/u32/u64), allman braces, 120 cols, LF
- After cvar registration, foreground will run microbench A/B (cvar 0
  vs 1) and confirm median/p99 frame_time_ms identical within jitter
- DO NOT flip default to 1 in this commit — that's A.4.2, a separate
  commit. Reason: clean revert path if microbench fails.

Verification before reporting done:
- make build-release succeeds (single serial invocation, NOT parallel)
- /Applications/OpenXRay-Dev.app launches, reaches CoP main menu
- console: `native_timing` returns 0; setting `native_timing 1`
  succeeds; setting `native_timing 0` works
- 5 min CoP gameplay default-cvar — no regression vs prior commit
```

После того как A.4.1 merged-to-branch и approved foreground'ом — dispatch
A.4.2 (одна строка default value flip + extended gate).
