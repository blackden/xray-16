# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Перед началом работы — ОБЯЗАТЕЛЬНО

В начале **каждой** сессии в этом репо **сразу** вызови project skill `xray-16-engine-work` через `Skill` tool, прежде чем делать что-либо ещё (включая чтение файлов или ответ на вопрос пользователя). Skill содержит project-specific build/run incantations, log paths, code conventions и pointer-index, которые иначе не загружены в твой контекст. Без него высок риск работы против стиля проекта.

Skill ссылается на `notes/reference/engine-map.md` как канонический индекс «где живёт X» — всегда заглядывай туда первым, прежде чем запускать Explore-агента. Карта верхнего уровня `notes/` — в [`notes/README.md`](notes/README.md).

## Проект

OpenXRay — это форк движка X-Ray 1.6.02 от GSC Game World (на нём работает S.T.A.L.K.E.R.: Call of Pripyat), поддерживаемый сообществом. Поддерживаются Call of Pripyat, Call of Chernobyl и Clear Sky; Shadow of Chernobyl пока не поддерживается. Кодовая база — C++17, кросс-платформенная (Windows/Linux/macOS/*BSD; x86/x64/ARM/ARM64/PPC64LE/E2K). Движок намеренно держится ванильного поведения — приоритет: исправление багов, производительность и качество кода, а не новые геймплейные фичи; новые геймплейные фичи делать опциональными, чтобы ванильные архивы `gamedata` продолжали работать.

Основная ветка разработки в апстриме — `dev`, именно туда уходят PR; иногда она может быть в неработоспособном состоянии.

## Сборка

В проекте параллельно поддерживаются две системы сборки:

- **CMake** (Linux/macOS/*BSD/Windows). Требует `cmake >= 3.23`.
  ```bash
  cmake -B build -DCMAKE_BUILD_TYPE=ReleaseMasterGold -DCMAKE_UNITY_BUILD=ON
  cmake --build build --parallel
  ```
  Подмодули должны быть подтянуты (`git clone --recursive` или `git submodule update --init --recursive`) — иначе CMake падает с явной ошибкой, потому что архив исходников не содержит нужных externals.
- **MSBuild** (только Windows). Откройте `src/engine.sln` в Visual Studio, или:
  ```cmd
  nuget restore src\engine.sln
  msbuild /m /p:Configuration="Release Master Gold" /p:Platform=x64 src\engine.sln
  ```
  Внимание: в MSBuild имя конфигурации с пробелами (`Release Master Gold`), а в CMake — слитно (`ReleaseMasterGold`).

Конфигурации сборки (есть во обеих системах):
- `Debug` — определяет `_DEBUG`, `DEBUG`; все ассерты и проверки.
- `Mixed` — определяет `DEBUG` и `MIXED`; debug-проверки + release-оптимизации.
- `Release` — определяет `NDEBUG`.
- `ReleaseMasterGold` (по умолчанию) — `NDEBUG` + `MASTER_GOLD`, исключения отключены (`XRAY_EXCEPTIONS=0`), у luabind отключены исключения и проверки ошибок. Это то, что уходит в релиз.

Полезные опции CMake: `XRAY_USE_LUAJIT` (ON), `XRAY_USE_ASAN`, `XRAY_ENABLE_TRACY`, `BUILD_SHARED_LIBS`, `XRAY_LINKER` (например, `lld`, `mold`).

Все артефакты сборки кладутся в `bin/<arch>/<config>/` (например, `bin/x86_64/ReleaseMasterGold/xr_3da`). Запускаемый исполняемый файл — `xr_3da`.

Runtime-зависимости на Linux: SDL2, lzo, libjpeg-turbo, OpenAL, libogg, libtheora, libvorbis. Точные имена пакетов под каждый дистрибутив есть в `.github/workflows/cibuild.yml`.

## Запуск

`xr_3da` выбирает игру через флаг командной строки, по умолчанию — Call of Pripyat:
- `-soc` — Shadow of Chernobyl (пока не поддерживается)
- `-cs` — Clear Sky
- (без флага) — Call of Pripyat
- `-fsltx <path>` — путь к `fsgame.ltx`, который указывает где лежат игровые данные
- `-overlaypath <path>` — перенаправляет `$app_data_root$` и `$logs$` на абсолютный путь, не трогая `fsgame.ltx` (реализовано в `src/xrCore/LocatorAPI.cpp:1041-1055`). Парсер использует `sscanf("%[^ ] ", ...)` — путь не должен содержать пробелов; для путей вида `~/Library/Application Support/...` оборачивайте через симлинк без пробелов.
- `-nogame` — запустить движок без `xrGame.dll`
- `-nointro` — пропустить интро

В `.vscode/launch.json` уже есть готовые конфиги gdb (SoC/CS/CoP/CoC), они ожидают переменные окружения `${SOC_LTX_FILE_PATH}` / `${CS_LTX_FILE_PATH}` / `${COP_LTX_FILE_PATH}`, указывающие на нужный `fsgame.ltx`.

## Тесты

Unit-тестов нет. CI проверяет только то, что сборка проходит на всей матрице из `.github/workflows/cibuild.yml`. Проверка — ручная: запустите `xr_3da` против известной установки игры.

## Линт / проверки стиля

CI запускает три проверки стиля через `.github/workflows/stylecheck.yml`. Прогоняйте их локально перед пушем — иначе PR упадёт:

- **spacing** — в файлах `.cs/.yaml/.yml/.md/.txt/.cmake/.sh` нет табов; ни в одном файле исходников/текста нет хвостовых пробелов; каждый файл оканчивается переводом строки. Исключены `Externals/`, `sdk/`, `res/`, `src/utils/mp_gpprof_server/libraries/`.
- **encoding** — UTF-8, LF переводы строк, без BOM.
- **clang-format** — `clang-format-20`, прогоняется через `git clang-format` относительно базового коммита. Конфиг лежит в `src/.clang-format` (clang-format 21+, C++17, отступ 4 пробела, 120 колонок, Allman-скобки). Коммиты типа «просто reformat» перечислены в `.git-blame-ignore-revs` — подключите его командой `git config blame.ignoreRevsFile .git-blame-ignore-revs`.

## Архитектура

Движок разбит на ~15 статических/динамических библиотек в `src/`. Сверху вниз:

- `xr_3da/` — крошечный шиппинговый исполняемый файл. `entry_point.cpp` создаёт `CApplication`, передаёт ему `xrGame` и доступные `RendererModule`-ы, и запускает. Никакой другой логики здесь нет.
- `xrEngine/` (`x_ray.h`/`CApplication`) — ядро движка: главный цикл устройства, консоль, ввод, диспетчер звука, окружение, камера, сетевая обвязка. `EngineAPI` и `EventAPI` — каркас межмодульных событий/RPC.
- `xrGame/` — геймплейный код (Actor, оружие, инвентарь, AI-агенты, диалоги, экраны UI). Большой; почти всё, что видит игрок, лежит здесь.
- `xrCore/` — фундамент: файловая система, строки, математика, debug/log, память, потоки, RTTI, сжатие, анимационные примитивы. От него зависит всё остальное.
- `xrAICore/` — поиск пути, графовый движок, фреймворк планировщика/операторов AI, используемый в `xrGame`.
- `xrCDB/` — база коллизий (broadphase + narrow phase на OPCODE).
- `xrPhysics/` — физика твёрдых тел и рэгдоллов поверх ODE.
- `xrSound/` — бэкенд OpenAL, стриминг, EFX/эффекты.
- `xrParticles/` — рантайм партиклов.
- `xrMaterialSystem/` — описания поверхностей/материалов, общие между рендером и геймплеем.
- `xrNetServer/` — сетевые примитивы (используются мультиплеером).
- `xrServerEntities/` — серверные определения сущностей, общие для клиента и выделенного сервера.
- `xrUICore/` — базовые UI-виджеты (сами экраны UI живут в `xrGame`).
- `xrScriptEngine/` — мост LuaJIT + luabind (deboostified), экспортирует API движка в Lua.
- `xrGameSpy/` — мультиплеерный мэтчмейкинг через GameSpy SDK.
- `Common/` — платформенные/компиляторные шимы только в заголовках (`Platform.hpp` определяет `XR_PLATFORM_*`, `XR_ARCHITECTURE_*`, `XR_COMPILER_*`). `Config.hpp` должен быть первым `#include` в каждом `.cpp` (или сразу после PCH).
- `utils/` — оффлайн-инструменты: `xrCompress`, `xrLC_Light` (запекание света), мультиплеерные хелперы.

**Рендеринг** — подключаемые `RendererModule` из `src/Layers/`:
- `xrAPI` — абстрактный интерфейс рендера.
- `xrRender_R2` / `xrRenderDX11` (`xrRenderPC_R4`) — бэкенды DirectX (только Windows).
- `xrRenderGL` / `xrRenderPC_GL` — бэкенд OpenGL (все платформы; на Linux/macOS/BSD — единственный рендерер).
- `xrRender` — код, общий для бэкендов.
Активный рендерер выбирается в рантайме из `s_render_modules` в `entry_point.cpp`, а соответствующий модуль собирается только на поддерживающих платформах (`if (WIN32)` в `src/Layers/CMakeLists.txt` отсекает DX).

**Подмодули** в `Externals/` (LuaJIT, luabind-deboostified, GameSpy, AGS_SDK, gli, sse2neon, sse2rvv, imgui-docking, xrLuaFix, плюс вендорные OPCODE/ODE/tracy/zlib/DiscordGameSDK/renderdoc) обязательны. Не редактируйте исходники подмодулей здесь — чините апстрим и обновляйте указатель.

## Кодстайл

Полные правила — в `doc/procedure/` (`common.txt`, `cpp_code.txt`, `cpp_comment.txt`, `commit_coding.txt`). Неочевидные моменты:

- C++17. Отступ 4 пробела, 120 колонок, LF, UTF-8, перевод строки в конце. Allman-скобки (`{` на новой строке для всех control flow и определений).
- `#pragma once`, никогда не include guards. В каждом `.cpp` первым (или сразу после PCH) идёт `#include "Config.hpp"`; заголовки должны компилироваться самостоятельно.
- Стиль `#include`: `"Foo.hpp"` для той же директории, `"SubDir/Foo.hpp"` относительно `src/`, `<foo>` только для системных. То есть внутри `src/xrCore/IO/` пишем `"IReader.hpp"`, а из другого модуля — `"xrCore/IO/IReader.hpp"`.
- Указатель/ссылка прижимаются к идентификатору: `int *p`, `const Foo &f`.
- Нейминг: PascalCase для классов/функций/публичных и protected-полей/глобалов/неймспейсов; camelCase для приватных полей и локальных. Lowercase допустим для математических примитивов (`vector3f`, `matrix44f.m03`) и алиасов примитивных типов (`uint`, `int16`). Венгерская нотация — *только* в UI-контролах (`btn`, `lbl`), у интерфейсных классов (`IClient`) и в template-параметрах (`TBase`).
- Особенность отступов, о которой стоит знать: вокруг тривиальной арифметики и тривиальных сравнений пробелов **нет** (`if (x>5)`, `Foo(a+b)`), вокруг операторов присваивания пробелы есть всегда, а у нетривиальных сравнений — есть (`if (x->y >= 5)`). Подробности — в `doc/procedure/cpp_code.txt`.
- Предпочитайте портируемые типы из `Common.hpp` (`u8`/`u32`/`pcstr` и т. п.) ОС-шным (`BYTE`/`DWORD`).
- Только строго типизированные enum (`enum class`) с запятой после каждого значения; явные числовые значения — если enum сериализуется.
- Предпочитайте стек; `delete` сам обрабатывает null — проверка перед ним не нужна.
- Переопределения помечайте `override`. У интерфейсных классов деструктор pure virtual + пустая inline-реализация вне класса. Не вешайте на интерфейсные классы linkage-атрибуты.
- Префикс имени модуля/проекта — `xr` (например, `xrCore`, `xrSound`). Файлы исходников — PascalCase; директория VS-проекта совпадает с именем проекта.
- Комментарии `XXX:` помечают известные проблемы, которые надо вернуться и доделать. Формат: `XXX [автор][/автор2] [КЛЮЧЕВЫЕ_СЛОВА]: текст`.

## Коммиты

`doc/procedure/commit_coding.txt`: краткое summary, описывающее *что и зачем* изменилось, а не какие файлы тронуты. Ссылки на issues — в конце: `Fix buffer overflow (close #45).` Несвязанные изменения разбивайте на отдельные коммиты.

Не мешайте «чисто reformat» коммиты с логическими изменениями; если делаете массовый reformat — добавьте SHA коммита в `.git-blame-ignore-revs`.
