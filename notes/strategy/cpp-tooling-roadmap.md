    Да, для C++ есть несколько слоёв линтеров и стоит подключить хотя бы
    базовые. Что использовать в порядке полезности:

    1. clang-tidy — главный аналог shellcheck для C++. Запускается на
    compile_commands.json (CMake его уже умеет генерить через
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON). Покрывает:
    - Modernize-чеки (auto, range-for, nullptr, override)
    - Performance (unnecessary-copy, move semantics)
    - Bugprone (sizeof-expression, dangling references, use-after-move)
    - readability, cppcoreguidelines
    - Memory safety паттерны
    В этой сессии в diagnostics уже видел clang-tidy warnings
    (bugprone-sizeof-expression в xrCore.cpp:588). То есть clang-tidy уже
    частично включён через LSP, но не как CI-gate. Стоит запустить полный прогон
     с настроенным .clang-tidy файлом в корне проекта.

    2. cppcheck — отдельный статический анализатор, complementary к clang-tidy.
    Ловит другой класс багов: null deref, uninitialized vars, leaks. Быстрый, не
     требует compile_commands.json.

    3. include-what-you-use (IWYU) — чистит лишние includes. У вас в этом репо
    LSP уже ругается на «unused-includes» в каждом файле (stdafx.h is not used
    directly). Это «false positive» для PCH, но реальные unused includes IWYU
    найдёт.

    4. Sanitizers через билд-флаги (это не линтеры, но дешёво и эффективно):
    - AddressSanitizer (UBSan) — уже есть XRAY_USE_ASAN в CMake флагах проекта
    (из CLAUDE.md). Стоит периодически билдить с ним и прогонять smoke.
    - ThreadSanitizer (TSan) — нашёл бы spatial DB race в #49 instantly. Не
    уверен есть ли в CMake опция, но добавляется одним флагом.

    Практический совет для xray-16:
    - Не подключать всё разом — кодбаза старая (2007 GSC код), линтер выдаст
    тысячи warning'ов и утопит сигнал.
    - Начать с clang-tidy на новых/изменённых файлах через git clang-tidy
    (аналог уже использующегося git clang-format). Покрывает только diff к base
    branch.
    - Добавить в CI stylecheck workflow (.github/workflows/stylecheck.yml) — там
     уже есть clang-format, рядом ляжет.
    - TSan-сборку — отдельный смоук-сценарий, не CI gate (медленно).

    Для xray-16 специфично: vendor'ные подмодули в Externals/ (luabind, gli,
    OPCODE, ODE) — обязательно exclude из линт-прогона. Они «не наши», править
    нельзя.

## QA automation tiers (parked vision)

*Перенесено из memory `project_test_rig_vision` 2026-05-24 при
консолидации MEMORY.md (issue #91). Парковка — будем подходить tier
за tier по мере того как soak harness даёт сигналы где не хватает
покрытия.*

Юзер 2026-05-20 после 100-cycle save/load soak: «раз ты можешь сделать
скрипт, значит можно будет написать программу управления для игры, с
аимом и так далее, чтобы проверять различные ситуации».

**Vision:** автоматизированный QA rig для CoP на macOS, отдельный Mac mini,
постоянно гоняющий тесты против последнего билда.

### Tier 1 — AppleScript key macros (current)

Soak harness (`scripts/soak/*`) — стартовая точка. Сильно ограничено
UI-навигацией и invariant'ами. Хорошо для:

- save/load cycles
- menu navigation
- dev-cvar toggling

Brittle if focus меняется или появляются unexpected modals.

**Quick wins сейчас:**
- Выбор non-hazard зоны для soak (Янов outdoor НЕ во время выброса).
- Immortality cvar для тестового сейва (юзер замечал что персонаж умирал
  в выбросе во время soak — save/load корректно работал, но cycle тестировался
  с persistent death state).

### Tier 2 — AppleScript + cliclick / Quartz Event Taps

WASD + relative mouse aim. Можно ходить по уровню, стрелять,
прицеливаться. Observations всё ещё через log scraping —
нельзя надёжно знать «персонаж попал в врага» без in-engine хука.

### Tier 3 — Lua in-engine test framework

CoP имеет LuaJIT + богатый API (`db.actor`, spawn, scheme triggers,
level-script hooks). Можно: ставить игрока в координату, спавнить
мобов, ждать условия, проверять здоровье / inventory / quest-state.
Это reactive test framework — наблюдаемый изнутри движка.

**Roadmap-уровень** (если когда-то возьмёмся):

1. `SOAK_LUA` cvar — позволяет engine исполнять `gamedata/scripts/test_*.script`
   пакеты по расписанию.
2. Библиотека test-helpers (`assert`, `expect`, `wait_for`,
   `with_god_mode`).
3. GitHub Actions / Gitea Actions runner на Mac mini, прогон на каждом
   push в `macos/blackden/master`.

### Resource consideration

Dedicated Mac mini понадобится свободный M-чип (M1/M2/M3). Поддержка
в форке уже есть; build pipeline существует (`make ship` ставит .app,
hooks автоматически).

### Out of scope until pursued

- Подсистема rendering screenshots для visual regression.
- Perf benchmarks (FPS floor / memory ceiling).
- Multiplayer test scenarios.
