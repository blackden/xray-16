# Сессия 01 — первый запуск OpenXRay на Apple Silicon (macOS 26.3)

Дата: 2026-05-17. Ветка: `macos/blackden/master`. Хост: MacBook Air, Apple M1, macOS 26.3 Tahoe (Darwin 25.3.0), Command Line Tools без полного Xcode.

Цель по плану из [progress.md](progress.md): пройти шаги 1–6 — поставить зависимости, собрать `Mixed`-конфигурацию, убедиться, что бинарь нативный arm64, запустить против CoP, зафиксировать baseline-поведение.

## Что получилось

- Бинарь собран: `bin/arm64/Mixed/xr_3da`, `Mach-O 64-bit executable arm64` — нативный arm64 ✅ (это и есть ключевая проверка шага 4).
- Движок стартует, инициализирует FS (38629 файлов из 29 архивов), видит OpenAL, обнаруживает GPU `[Apple] device: [Apple M1]` через GL 4.1 Metal.
- Падает в `CEngineAPI::SelectRenderer` ровно из-за того, что в данных нет GL-шейдеров (см. ниже).

Сырые артефакты сессии: [notes/session-20260517-001502/](session-20260517-001502/) — `stdout.log`, `system.txt`.

## Хронология проблем и фиксов

Маршрут оказался не «один раз `make all` и поехали» — на каждом шаге всплывал баг в обвязке. По порядку:

### 1. `make run` → «submodules missing», хотя подмодули были

```
ERROR: submodules missing (run 'make setup' or 'git submodule update --init --recursive')
```

`git submodule status` показывал все 9 модулей. Sentinel в Makefile (`check-configure-prereqs`) проверял существование `Externals/luabind-deboostified/CMakeLists.txt`, а реальный локальный путь подмодуля — `Externals/luabind/` (URL ведёт на `luabind-deboostified.git`, путь чекаута — `luabind`).

**Фикс:** [Makefile:65](../Makefile:65) → проверяем `Externals/luabind/CMakeLists.txt`. Коммит `af253818d`, запушен.

### 2. `make run` → `cmake -B  -DCMAKE_BUILD_TYPE=Mixed ...` (пустой `-B`)

`.env`, сгенерированный ранее, содержал:
```
BUILD_DIR = ${BUILD_DIR:-build}
```
Это shell-синтаксис (parameter expansion). Make парсит `${BUILD_DIR:-build}` как обращение к переменной с именем `BUILD_DIR:-build`, которой нет, и подставляет пустую строку. Дефолт `BUILD_DIR ?= build` уже не отрабатывает, потому что переменная *определена* (как пусто).

**Фикс:** локально в `.env` → `BUILD_DIR = build`. Сам `.env` гитигнорится; в `.env.example` всё было корректно — это была опечатка в моём `.env`.

### 3. CMake-конфигурация → `clang: invalid version number in 'MACOSX_DEPLOYMENT_TARGET=25.3.0'`

`cmake/XRay.Compiler.GNULike.cmake` дефолтил `CMAKE_OSX_DEPLOYMENT_TARGET` в `${CMAKE_SYSTEM_VERSION}`. На macOS `CMAKE_SYSTEM_VERSION` — это Darwin kernel (25.3.0), а clang ждёт macOS product version (26.3 для Tahoe). На «нормальных» версиях macOS до Sequoia kernel-номер случайно совпадал с product-номером и баг не вылезал.

**Фикс:** [cmake/XRay.Compiler.GNULike.cmake:8](../cmake/XRay.Compiler.GNULike.cmake:8) — читаем версию через `sw_vers -productVersion` (даёт `26.3`).

### 4. Параллельно: `Failed to open property list /Applications/Xcode.app/.../SDKSettings.plist`

Из под-сборки LuaJIT. У меня только Command Line Tools, `Xcode.app` не установлен. Источник — захардкоженный путь:

```cmake
# Externals/LuaJIT-proj/CMakeLists.txt:18 (было)
set(CMAKE_OSX_SYSROOT "/Applications/Xcode.app/.../MacOSX.sdk")
```

**Фикс:** [Externals/LuaJIT-proj/CMakeLists.txt:18](../Externals/LuaJIT-proj/CMakeLists.txt:18) — SDK определяется динамически через `xcrun --sdk macosx --show-sdk-path`. Если `xcrun` не отвечает или путь не существует — пусть CMake находит сам.

### 5. Сборка прошла, но `make build` падает на верификации

```
[100%] Linking CXX executable /Users/ragnar/.../bin/arm64/Mixed/xr_3da
[100%] Built target xr_3da
ERROR: xr_3da not found under build
```

CMake кладёт артефакты в `bin/<arch>/<config>/`, а не в `build/` (это прямо написано в [CLAUDE.md](../CLAUDE.md)). Verify-шаг в Makefile искал под `$(BUILD_DIR)`.

**Фикс:** [Makefile:76,95](../Makefile:76) — оба `find` теперь смотрят в `bin/`. `clean` тоже сносит `bin`.

### 6. Запуск → `~ No shaders found for OpenGL` → `Can't setup renderer`

Полный хвост лога — см. [notes/session-20260517-001502/stdout.log](session-20260517-001502/stdout.log). Самое интересное:

```
* GPU vendor: [Apple] device: [Apple M1]
* GPU OpenGL version: 4.1 Metal - 90.5
Available render modes[1]: renderer_r3
~ No shaders found for OpenGL

FATAL ERROR
Expression : selectedRenderer
File       : src/xrEngine/EngineAPI.cpp:88
Description: Can't setup renderer
```

Это **не** баг движка — это вопрос данных. Steam-инсталл CoP кладёт всё в `.db*`-архивы (`levels/`, `resources/`, `localization/`, `patches/`, `mp/`), а `gamedata/` как такового нет. Шейдеры в ванильных архивах только DirectX. OpenXRay поставляет GL-шейдеры отдельно — внутри репо, в `res/gamedata/shaders/gl/` (плюс свои конфиги и скрипты рядом).

В `fsgame.ltx` `$game_data$ = $fs_root$/gamedata/`. Решение — оверлей: симлинкнуть `res/gamedata` в `$(GAME_DIR)/gamedata`, и движок будет видеть и архивы (ванильные данные), и оверлей (GL-шейдеры).

**Фикс:** добавлен таргет `make link-gamedata` ([Makefile:152-167](../Makefile:152)) — делает симлинк, проверяя что ничего не затирает (если по пути уже не-симлинк — отказывается). Запустил один раз: `/Users/ragnar/Games/STALKER-CoP/gamedata -> /Users/ragnar/fedorov_tech/xray-16/res/gamedata`.

### 7. Ретест после `link-gamedata` → краш в GameSpy ATLAS

Артефакты: [notes/session-20260517-001840/](session-20260517-001840/).

Движок прошёл *далеко* дальше:

- Рендерер выбрался: `Selected renderer: renderer_r3`.
- Закомпилилось ~80 GL-шейдеров (`gl\dumb.ps`, `gl\accum_*`, `gl\bloom_*`, `gl\combine_*`, `gl\particle*`, `gl\sky2`, `gl\clouds`, и т. д.) — оверлей сработал.
- Загрузились скрипты движка и main-menu (`ui_main_menu.script` и далее).
- Звук инициализировался, `* DVB created: 4096K` / `* DIB created: 512K` — рендер-устройство стартовало.

Падение:

```
[error] Expression : init_res == SCResult_NO_ERROR
[error] File       : src/xrGameSpy/GameSpy_ATLAS.cpp:21
[error] Description: assertion failed
! GameSpy ATLAS: failed to initialize, error code: 4

stack trace:
CGameSpy_ATLAS::Init()
CGameSpy_ATLAS::CGameSpy_ATLAS()
CGameSpy_Full::CGameSpy_Full()
CMainMenu::CMainMenu()
CGamePersistent::OnAppStart()
```

GameSpy — это матчмейкинговый сервис EA, окончательно выключенный в 2014. Инициализация в 2026 всегда вернёт ошибку. Сам по себе это ожидаемо. Что неприятно — `CMainMenu` в конструкторе создаёт `CGameSpy_Full → CGameSpy_ATLAS`, и до меню одиночного CoP мы не доходим.

Смотрим в код ([src/xrGameSpy/GameSpy_ATLAS.cpp:18-26](../src/xrGameSpy/GameSpy_ATLAS.cpp:18)):

```cpp
void CGameSpy_ATLAS::Init()
{
    SCResult init_res = scInitialize(GAMESPY_GAMEID, &m_interface);
    VERIFY(init_res == SCResult_NO_ERROR);       // <-- ассертит
    if (init_res != SCResult_NO_ERROR)            // <-- но логирование ниже
    {
        Msg("! GameSpy ATLAS: failed to initialize, error code: %d", init_res);
    }
}
```

Логически противоречит самому себе: `if`-блок описывает «при ошибке — залогируй и продолжай», но `VERIFY` выше отменяет это в Mixed-сборках. В `ReleaseMasterGold` `VERIFY` скомпилён прочь, и движок бы шёл дальше.

**Фикс:** убрать `VERIFY`, явно сбросить `m_interface = NULL` в случае ошибки. MP-пути, насколько видно из API класса, и так должны на NULL отрабатывать (если упадут — увидим следующим заходом).

### 8. Ретест после фикса GameSpy → дошли до меню, краш на «Start Game»

Артефакты: [notes/session-20260517-002339/](session-20260517-002339/).

Положительное:
- Прошли инициализацию main menu (GameSpy теперь шумит `! GameSpy ATLAS: failed to initialize, error code: 4` и идёт дальше).
- Главное меню реально отрисовалось: загружены `ui_actor_main_menu.dds`, шрифты, курсоры, `ui_font_hud_01.dds`.

Симптом: пользователь нажал «Start Game» — окно зависло и само закрылось. Процесс не остался в `Stopped` — он просто умер.

Что важно:
- В `stdout.log` нет секции `FATAL ERROR`/`stack trace`. Движок не дошёл до своего `xrDebug` обработчика — значит сигнал поймала ОС (SIGSEGV/SIGBUS/SIGABRT).
- В `~/Library/Logs/DiagnosticReports/` тоже пусто. На неподписанных бинарях macOS иногда не пишет `.ips` — известная штука.
- Engine log `_appdata_/logs/openxray_*.log` обрывается прямо посреди строки: `simple_color.ps\02048111100110000000000001000001311112311` — буфер не успел сброситься. Это ровно подпись «убит сигналом без unwind».

Параллельные находки по обвязке:
- `make run` искал engine-логи в `$(dirname FSGAME_LTX)/logs/`, а реальный путь — `$(GAME_DIR)/_appdata_/logs/`. Поправил: теперь смотрит оба места.
- Добавлен таргет `make run-lldb` — лаунчер под `lldb`, чтобы поймать сигнал в момент падения и распечатать `bt all`. Это единственный реалистичный путь дальше — без стектрейса работающее меню → невидимый краш отдельной гипотезы не даёт.

**Фикс пока нет** — нужна диагностика. План: `make run-lldb FSGAME_LTX=...`, повторить «Start Game», по падению дать `bt all`, посмотреть в каком модуле упало (`xrGame`, `xrRenderGL`, `xrCore`?), оттуда решать.

### 9. Запуск под lldb → игра работает

`make run-lldb FSGAME_LTX=...`. Пользователь увидел загрузку, дошёл до игры, полетал, поговорил с NPC, вышел из меню. Терминал:

```
* [1] KERNEL:QUIT
----total count =  0
Process 70827 launched: '.../bin/arm64/Mixed/xr_3da' (arm64)
Process 70827 exited with status = 0 (0x00000000)
```

То есть **движок собирается и запускает CoP на Apple M1 / macOS 26.3 Tahoe end-to-end**. Цель первой сессии (план шагов 1–6 из [progress.md](progress.md)) выполнена и перевыполнена.

Что в этой сессии ушло из крашей предыдущего прогона — пока непонятно. Возможные объяснения: (а) под `lldb` тайминги другие (отладчик меняет окружение / выключает оптимизацию малтипоточного запуска), (б) случилось «случайное» — гонка где-то в инициализации сессии, которая выстрелила один раз. Это придётся записать как **открытый вопрос**: воспроизводится ли краш при чистом `make run` без `lldb` после `Start Game`?

Косметический фикс по дороге: `run-lldb` теперь `--batch` с `-k` (run-on-error), чтобы не плеваться `Command requires a process which is currently stopped` при чистом выходе.

### 10. Дальше: «краш» оказался чистым exit'ом

Контекст: после фикса compressed-3D-текстур (коммит `b00e9c786`) `make run` стабильно «вылетает» во время prefetch'а нового загружаемого уровня (NPC face-текстуры, потом weapon-текстуры — точка движется). lldb-прогон всё так же воспроизводит без крашей.

Подписали бинарь через `make codesign` (новый таргет, [scripts/mac/debug.entitlements](../scripts/mac/debug.entitlements) с `com.apple.security.get-task-allow=true`) — `.ips` всё равно не появился. Полезли в `log show`.

Что нашли в unified log (PID 83346 = xr_3da):

```
01:19:58.4  WindowServer: normalDeathNotification ... xr_3da
01:19:58.5  WindowServer: Closing conn 0xac28f, PID 83346
01:20:00.9  runningboardd: termination reported by proc_exit
            launchservicesd: LSExitStatus=0
```

Выводы:
- **Не краш.** Никакого сигнала, никакого SIGSEGV/jetsam. `proc_exit` + `exitStatus=0` — это штатный `exit()` (или `_exit()`).
- **Window connection** закрылся за **~2 секунды до** `proc_exit`. То есть GL-окно отвалилось от WindowServer'а, а движок ещё успел доделать что-то в памяти и потом тихо вышел.
- Engine-лог обрывается посреди строки потому, что `_exit()` не сбрасывает stdio-буферы. Это не «убит сигналом», как мы думали; это «вышел через путь, минующий нормальный shutdown».
- `.ips` нет потому, что **краша нет** — ReportCrash нечего записывать. Codesign-таргет всё равно полезен для будущих *настоящих* падений.

**Ложный шаг по дороге:** в `log show` сначала я увидел `ReportCrash: PID 83348 exceeded the memory high watermark` и поспешил объявить OOM-jetsam. Оказалось, **83348 — это `XprotectService`** (Apple-овский антивирус), а xr_3da был PID **83346**. Разные процессы. Перепроверять PID — must.

**Что искать дальше:** во время prefetch'а кто-то из этих путей триггерится:
- Alt-F4 — не наш случай (пользователь не нажимал).
- Window close button (`SDL_WINDOWEVENT_CLOSE` → `device.cpp:388 Event.Defer("KERNEL:quit")`) — окно ведь само закрылось, могло быть.
- Console-команда `quit` (`xr_ioc_cmd.cpp:71`).
- Прямой `exit()` / `_exit()` из движка/драйвера/SDL2 в ответ на ошибку GL.

Гипотеза №1: **Apple GL/Metal во время prefetch'а ловит ошибку (out-of-memory на GPU, или внутренний assert), драйвер закрывает GL-контекст → SDL2 получает window-close → `KERNEL:quit` → штатный shutdown без вывода `FATAL ERROR`**. Под lldb проходит, потому что lldb меняет тайминги/планирование Metal так, что предел не достигается.

Гипотеза №2: где-то в `xrCore`/`xrEngine` есть прямой `exit()` на условие, которое теперь срабатывает. Грепнуть `exit(` / `_exit(` в этих модулях.

**Инструментация на следующий заход:**
- В [src/xrEngine/Engine.cpp:102](../src/xrEngine/Engine.cpp:102) (handler eQuit) добавить `Msg("==> KERNEL:quit fired"); FlushLog();` чтобы знать, что вообще через эту ветку прошло.
- В `SDL_WINDOWEVENT_CLOSE` в [device.cpp:380](../src/xrEngine/device.cpp:380) — то же самое.
- `grep -rn 'exit(\|_exit(' src/xrEngine src/xrCore src/Layers/xrRenderGL` — найти все возможные ранние выходы.

## Состояние репо к концу сессии

Закоммичено и запушено:
- `af253818d` — Fix Makefile submodule sentinel to match actual path.

Локально (не закоммичено, ждёт обсуждения / следующего ретеста):
- `cmake/XRay.Compiler.GNULike.cmake` — `sw_vers -productVersion`.
- `Externals/LuaJIT-proj/CMakeLists.txt` — `xcrun --show-sdk-path`.
- `Makefile` — verify/run ищут в `bin/`, `clean` чистит `bin`, новые таргеты `link-gamedata` и `run-lldb`, engine-logs копируются из `_appdata_/logs/`.
- `src/xrGameSpy/GameSpy_ATLAS.cpp` — убран фатальный `VERIFY`, ошибка инициализации не валит main menu.

Не в гите (по дизайну):
- `.env` — поправлен `BUILD_DIR`.
- `build/`, `bin/` — артефакты сборки.

## Извлечённые уроки

Полезно держать в голове на следующий заход:

- **macOS 26 Tahoe = Darwin 25.x** — где-то ещё в кодовой базе может ловиться эта же ловушка с `uname -r` / `CMAKE_SYSTEM_VERSION`. Если что-то загадочно падает с числами около 25.x — это оно.
- **Только CLT, без Xcode.app** — в репо ещё могут быть хардкоды на `/Applications/Xcode.app`. Когда увидим — поправим аналогично.
- **CoP из Steam ≠ распакованный CoP**. Всё в `.db*`. `gamedata/` создаём сами под оверлей.
- **`renderer_r3` в OpenXRay GL-сборке — это OpenGL**, а не DX10. Названия унаследованы от ванильного движка, не сбивайся.
- **Шаг verify в Makefile падал не потому что сборка не прошла**, а потому что искал не там. Лог `[100%] Linking ... xr_3da` — он уже про success. Сначала читать лог сборки, потом верить ошибке верификатора.

## Следующая сессия

1. Прогнать `make run` ещё раз — теперь должно дойти дальше выбора рендерера. Куда — узнаем.
2. Закоммитить пачку фиксов из этой сессии (cmake/xrun/sw_vers, Makefile bin/, link-gamedata). Резонно одним коммитом «macOS Tahoe / CLT-only bring-up fixes», или разбить по теме. Предложить пользователю выбор.
3. Если упало — записать новый `notes/session-NN.md` и категоризировать падение по модулю в стектрейсе (план из [progress.md](progress.md) и [apple-silicon.md](apple-silicon.md)).

Точка возобновления: «продолжаем по `notes/session-01.md`, последний прогон — `notes/session-<timestamp>/`».
