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
