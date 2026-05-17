# Roadmap

Открытые задачи в порядке убывания пользы для нашей цели (поиграть на macOS, в идеале на максимальных настройках, нативно).

## Краткосрочно (часы-дни)

- **Упаковка нашей сборки в `.app` бандл** — Info.plist, copy dylibs, install_name_tool rpath, codesign. Шаблон взят у [Mac Source Ports build script](https://github.com/MacSourcePorts/MSPBuildSystem/blob/main/xray-16/macsourceports_arm64.sh). **Готово**: `make package` → `dist/OpenXRay.app` с иконкой Monolith и launcher-shim'ом. См. [notes/macos-build-guide.md](macos-build-guide.md).
- **Локализация мелочей в `res/gamedata/configs/text/rus/openxray.xml`** — "Выйти в Windows" → "Выйти в macOS" (частично сделано; работает в Clear Sky UI style, в дефолтном CoP menu пока остаётся). Подробности в "Известные баги".
- **DMG для брата с бандленными игровыми данными** — **Готово**. `make all-in-one` собирает `dist/OpenXRay-AllInOne.dmg` (~4.5 GB UDZO): `OpenXRay.app` + `STALKER-CoP/` side-by-side. Получатель перетаскивает обе иконки в `/Applications/`, кликает по `.app`, играет. Сейвы и логи — `~/Library/Application Support/OpenXRay/` через `-overlaypath` (см. [macos-build-guide.md](macos-build-guide.md) раздел «Вариант "всё в одном"»).
- **Cocoa Cmd+Q shim (experimental)** — `src/xrEngine/macos_cocoa_shim.mm` написан и установлен в бандл, но **runtime НЕ верифицирован**. Замысел: первое Cmd+Q → синтез `SDLK_ESCAPE` (открыть pause-меню), второе в течение 3 с → `_exit(0)`. Реализован двойной intercept: NSEvent local key-down monitor (primary) + `applicationShouldTerminate:` override (fallback для menu-driven quit). Первая (delegate-only) ревизия не перехватывала Cmd+Q вообще (логи показывают install, но handler не вызывается); NSEvent-вариант собран, но пользователь не прогнал smoke-test (поставил на паузу: «украшательский скрипт»). Возвращаемся когда стабильность движка позволит спокойно тестировать без зависаний. Связано с overflow `xrDebug::Fail` (см. среднесрочно) — shim только обходит этот баг, не чинит.

## Среднесрочно (недели)

- **Multi-frame для других hot spot'ов**:
  - Lua script bulk loading (~70 скриптов подряд)
  - Spawn registry / AI space load
  - Texture load в options menu
  Сейчас живёт за счёт того, что Release-сборка не вылетает на debug-asserts; но в любой момент может стрельнуть hang detection.
- **Кириллица в saves** — выяснить точный сценарий (см. отдельный пункт ниже).
- ~~**Зафиксить настоящий buffer overflow в `xrDebug::Fail`**~~ — **Готово**, `src/xrCore/xrDebug.cpp`. Корень: в `GatherInfo` использовался unsafe pattern `buffer += xr_sprintf(buffer, oneAboveBuffer - buffer, ...)` — `vsnprintf` возвращает «сколько байт *хотел бы* написать» при truncation; стоит одному вызову обрезаться, `buffer` уходит ЗА `oneAboveBuffer`, `oneAboveBuffer - buffer` underflow'ит как `size_t` в гигантское значение, следующая итерация пишет в стек unbounded → ломает saved LR / PAC slot → PAC trap на return из `Fail` на ARM64 macOS. На Windows маскировалось BugTrap short-circuit'ом до stack-trace секции. Заодно починён `ErrorToString` для non-Windows (возвращал `nullptr`, что приводило к `strchr(nullptr, ...)` UB). Это **upstream-достойный фикс** — баг латентный на всех платформах, на macOS просто проявляется как краш из-за PAC. Связано с Cmd+Q — после этого фикса Cocoa shim (см. краткосрочно) скорее всего избыточен и может быть revert'нут.
- ~~**Зафиксить ранний return в `CLocatorAPI::rescan_path`**~~ — **Готово**, `src/xrCore/LocatorAPI.cpp` ~1925. Убран ранний `return` при `lower_bound == end()`. До фикса: новый FS-path (`-overlaypath` для `$app_data_root$`) **никогда не индексировался** → `FS.exist` возвращал false для файлов в overlay → `cfg_load` падал в `$fs_root$` fallback (stale bundled), а сохранения вообще не находились (для `$game_saves$` fallback-цепочки нет). После фикса `Recurse(full_path)` вызывается безусловно. Кандидат в апстрим — баг затрагивает любую платформу, использующую runtime FS-path добавление, не только macOS.
- ~~**Зафиксить mixed separators в `FS_Path::_set_root`**~~ — **Готово**, `src/xrCore/LocatorAPI_defs.cpp` ~83. Добавлен `restore_path_separators(temp)` сразу после копирования входного `root`, чтобы поведение соответствовало конструктору `FS_Path::FS_Path` (он этот вызов делает). До фикса: при `-overlaypath /Users/...` (POSIX-форма) `m_Path` оставался mixed (`/users/.../\_appdata_\`), а индексируемые через `Recurse`+`Register` файлы нормализовались на all-`\` (`\users\...\_appdata_\user.ltx`). При lookup `m_files.find` сравнивал строки байт-в-байт → mismatch → файл «не найден» в кэше → `cfg_load` всё равно падал в `$fs_root$` fallback. Этот баг полностью маскировал результат первого фикса. Связанная пара — упоминается вместе как «overlay indexing bug» в апстрим-PR.

## Долгосрочно (месяцы)

- **Новый рендер: Metal или Vulkan через MoltenVK** — единственный путь к "max settings" без glitch'ей света и без compatibility-патчей. Сейчас Apple OpenGL 4.1 (Metal-backed) — это shim с дырами:
  - Compressed 3D textures не поддерживаются → water_sbumpvolume FATAL
  - MSAA framebuffer attachments кривые → set_RT FATAL
  - Light glitches зависящие от ракурса (наблюдается)
  - Низкий теоретический потолок производительности
  Объём: 2-4 месяца на Vulkan backend, 3-6 на Metal. Vulkan приоритетнее — польза и Linux, и шанс попасть в апстрим.

## Известные открытые баги

- **Скриншоты сохранений не пишутся на диск на macOS** — APFS отвергает cp1251 байты из localized сохраняющего тега (`начало игры` и т.п.) как невалидный UTF-8 → EILSEQ. Сам `.scop` пишется (отдельным path'ом), но `.dds` thumbnail — нет. Корень: движок передаёт cp1251 строки прямо в `FS.update_path`→`fopen`. Фикс — добавить cp1251→UTF-8 конвертацию на границе FS API на macOS. Несколько часов аккуратного аудита всех write путей с cp1251 строками.
- **Кириллица в `Core.UserName` отключена на macOS** — берём `pw_name` (ASCII login) вместо `pw_gecos` (UTF-8 real name). Иначе UI рендерит mojibake + EILSEQ на FS-путях. Связано с предыдущим пунктом — после фикса cp1251→UTF-8 на границах можно вернуть `pw_gecos`.
- **"Выйти в Windows" в дефолтном CoP main menu** — наш overlay `ui_mm_quit2windows="Выйти в macOS"` срабатывает только для Clear Sky UI style (`styles_/ui_style_cs/`). Дефолтный CoP main menu (`ui_mm_main.xml`) живёт внутри `.db*` архивов и использует другой string id для quit-кнопки и подтверждающего диалога (`message_box_quit_windows`). Чинится одним из двух:
  - Распаковать `.db` (упаковщик `src/utils/xrCompress`, распаковщика готового нет — нужно либо использовать сторонний `db_unpacker`, либо проинструментировать движок чтобы он дампил), найти string id, добавить в `res/gamedata/configs/text/rus/openxray.xml`
  - Положить полный overlay `res/gamedata/configs/ui/ui_mm_main.xml` с нашим caption — но это копия 100+ строк, придётся синхронизировать с апстримом
- **Light glitches** на Скадовске — зависит от угла камеры. Корень — Apple GL shim.
- **Cmd+Q крашит** (наследие MSP build известная проблема, наш build тоже подвержен) — это PAC trap в xrDebug::Fail когда движок инициирует terminate. Experimental shim (см. краткосрочно) пытается обойти, но не верифицирован. До решения: пользоваться внутриигровым «Выйти в Windows» (стабильно), а Cmd+Q не нажимать без необходимости.

## Чего движок не может вылечить (kernel-level)

- **TX-state процесс после GPU driver hang** — типичный сценарий: загрузка уровня с max settings, Apple GL shim уводит процесс в IOKit wait, `ps -ax -o stat` показывает `TX`. Никакой `kill -9` из shell, никакой `sudo`, никакой app-side код процесс не вытащит — пока kernel держит handle на GPU, процесс висит в неинтерраптерабельном состоянии. Recovery пути по убыванию инвазивности:
  - подождать 30-90 секунд watchdog'а GPU драйвера (иногда отпускает, тогда работает обычный `kill -9`);
  - **Cmd+Opt+Esc** → Force Quit dialog (требует чтобы окно было обычным NSWindow, не fullscreen — поэтому launcher шим форсит windowed mode);
  - **Activity Monitor** → Force Quit (тот же путь, иногда срабатывает когда Cmd+Opt+Esc нет);
  - **logout сессии** (`Shift+Cmd+Q`) — kernel при logout агрессивнее прибивает процессы, чаще отпускает;
  - **hard reset** (hold power 10 секунд) — гарантия, но дорого.
  Снизить incidence можно только устранением операций, провоцирующих GPU hang (compressed 3D textures, MSAA framebuffer attachments — см. долгосрочно про Vulkan/Metal). Это не задача user-space обвязки.

## Найдено в smoke-test all-in-one DMG (2026-05-17)

- **[P0, починено — ждёт repeat smoke-test]** ~~Сброс настроек игры на macOS~~ — было: игрок меняет язык/графику/громкость, выходит, перезапускает — всё сбрасывается. Та же причина и для **сохранений игры** (которые тоже не персистились). **Два сцепленных бага**: (1) `rescan_path` early-return → overlay никогда не индексировался, (2) `_set_root` не нормализовал separators → даже после ручного rescan имена в `m_files` не матчились с тем что строит `_update` при lookup. Первый фикс был сделан в прошлой сессии, но симптом не ушёл из-за второго. Сейчас оба фикса в working tree + бинарь в /Applications/OpenXRay.app/. Launcher-workaround для user.ltx удалён.
- **[P0, ждёт repro] Crash при изменении некоторых параметров графики** — пользователь боится менять, потому что есть параметры которые крашат игру. Нужно: (а) от пользователя — список конкретных параметров, которые крашат, и stack trace из `~/Library/Application Support/OpenXRay/logs/xray_*.log`; (б) идея на будущее — спрятать на macOS опции, которые мы заведомо знаем, что крашат (требует enum'а опций в UI XML и matching на стороне движка).
- **[P2] «DX10» в UI выбора рендера на macOS** — косметика. На macOS у нас в `s_render_modules` (entry_point.cpp) только GL. Либо UI XML хардкодит названия рендеров, либо GL-бэкенд репортится движком как «R3», а UI-mapping показывает «R3» = «DX10». Требует чтения `s_render_modules` инициализации + соответствующего UI XML (вероятно `res/gamedata/configs/ui/ui_options*.xml` или `ui_mm_*.xml`).
- **[P2, ждёт repro] Пространственное аудио (EFX/HRTF) на macOS** — пользователь подозревает что работает не как надо. Конкретики нет. Проверка: `brew install openal-soft && openal-info`; чтение `src/xrSound/` инициализации OpenAL Soft на macOS; smoke-test со сравнением «звук с EFX» vs «звук без EFX» через консольную команду `snd_efx`.

## Что сейчас работает (на момент 2026-05-17)

- Native macOS сборка `Mixed` под Apple Silicon из ветки `macos/blackden/master`.
- MSP Release Master Gold билд (без наших фиксов) играется end-to-end.
- Все коммиты на ветке `macos/blackden/master`:
  - `911bb27d7` Makefile + Brewfile
  - `7cf5523e7` steamcmd helper
  - `b6266c4e2` install-game target
  - `3196bf416` .env override
  - `345d0e2b9` glTexture.cpp VERIFY → log
  - `b2c4e4d2f` diagnostic markers (не для апстрима)
  - `5f3ca8307` session-01.md журнал
  - `bf2fcef84` **multi-frame prefetch** (кандидат в апстрим)
  - `a04f5d37f` **CHK_GL softening на Apple** (кандидат в апстрим)
