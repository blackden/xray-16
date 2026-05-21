# Roadmap

Открытые задачи в порядке убывания пользы для нашей цели (поиграть на macOS, в идеале на максимальных настройках, нативно).

**Shipping definition**: что считать «1.0» этого форка — см.
[`done-criteria.md`](done-criteria.md). Roadmap описывает «что делаем»,
done-criteria — «когда останавливаемся».

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
- **TX-state при загрузке уровня (max settings)** — диагностика `legacy-analyst` 2026-05-18 указывает на **`R_occlusion::occq_get` + `R_sync_point::Wait`** как наиболее вероятные сайты. Каждый блокирующий `glGetQueryObjectiv(GL_QUERY_RESULT)` на Apple GL делает `mach_msg`-ожидание GPU. На первой precache frame (60 шт. с поворотом камеры 360°, `game_sv_single.cpp:344`) куча occlusion queries × shadow cascades × lights → ожидания компаундируются. Запланировано/сделано:
  - ✓ **Non-blocking occlusion queries** (этот фикс): `QueryHelper.h::GetData` для GL теперь polls `GL_QUERY_RESULT_AVAILABLE`, возвращает `S_FALSE` если результат не готов — caller (`R_occlusion::occq_get`) уже имеет loop с `SwitchToThread`/`Sleep`/`500ms timeout`, который раньше был мёртвым кодом на GL.
  - ✓ **CHK_GL diagnostic logging на Apple в MasterGold** (этот фикс): раньше в release-билде CHK_GL был bare `expr` без проверки `glGetError`. Теперь на Apple логирует `Msg("! OpenGL: %s -> 0x%X", ...)` всегда — даёт нам post-mortem-видимость когда машина зависает.
  - **TODO**: reduce `PreCache(60, true)` на Apple до 20 — меньше работы для GPU в первый момент после загрузки. `src/xrGame/game_sv_single.cpp:344`.
  - **TODO**: добавить `glFinish()` в конце `Prefetch_Tick` (когда `done==true`, `IGame_Persistent.cpp:455`) — гарантировать что upload commands драйнили до начала первого рендера.
  - **TODO**: проверить `glClientWaitSync` на Apple — math timeout'а корректна (`ms * 1e6 = ns`, дефолт 500ms), но сам Apple GL shim может игнорировать timeout и блокировать в `mach_msg` дольше. Если CHK_GL logs покажут что fence syncs возвращают `GL_TIMEOUT_EXPIRED` много раз — это подтверждение.
- **Кириллица в saves** — выяснить точный сценарий (см. отдельный пункт ниже).
- ~~**Зафиксить настоящий buffer overflow в `xrDebug::Fail`**~~ — **Готово**, `src/xrCore/xrDebug.cpp`. Корень: в `GatherInfo` использовался unsafe pattern `buffer += xr_sprintf(buffer, oneAboveBuffer - buffer, ...)` — `vsnprintf` возвращает «сколько байт *хотел бы* написать» при truncation; стоит одному вызову обрезаться, `buffer` уходит ЗА `oneAboveBuffer`, `oneAboveBuffer - buffer` underflow'ит как `size_t` в гигантское значение, следующая итерация пишет в стек unbounded → ломает saved LR / PAC slot → PAC trap на return из `Fail` на ARM64 macOS. На Windows маскировалось BugTrap short-circuit'ом до stack-trace секции. Заодно починён `ErrorToString` для non-Windows (возвращал `nullptr`, что приводило к `strchr(nullptr, ...)` UB). Это **upstream-достойный фикс** — баг латентный на всех платформах, на macOS просто проявляется как краш из-за PAC. Связано с Cmd+Q — после этого фикса Cocoa shim (см. краткосрочно) скорее всего избыточен и может быть revert'нут.
- ~~**Зафиксить ранний return в `CLocatorAPI::rescan_path`**~~ — **Готово**, `src/xrCore/LocatorAPI.cpp` ~1925. Убран ранний `return` при `lower_bound == end()`. До фикса: новый FS-path (`-overlaypath` для `$app_data_root$`) **никогда не индексировался** → `FS.exist` возвращал false для файлов в overlay → `cfg_load` падал в `$fs_root$` fallback (stale bundled), а сохранения вообще не находились (для `$game_saves$` fallback-цепочки нет). После фикса `Recurse(full_path)` вызывается безусловно. Кандидат в апстрим — баг затрагивает любую платформу, использующую runtime FS-path добавление, не только macOS.
- ~~**Зафиксить mixed separators в `FS_Path::_set_root`**~~ — **Готово**, `src/xrCore/LocatorAPI_defs.cpp` ~83. Добавлен `restore_path_separators(temp)` сразу после копирования входного `root`, чтобы поведение соответствовало конструктору `FS_Path::FS_Path` (он этот вызов делает). До фикса: при `-overlaypath /Users/...` (POSIX-форма) `m_Path` оставался mixed (`/users/.../\_appdata_\`), а индексируемые через `Recurse`+`Register` файлы нормализовались на all-`\` (`\users\...\_appdata_\user.ltx`). При lookup `m_files.find` сравнивал строки байт-в-байт → mismatch → файл «не найден» в кэше → `cfg_load` всё равно падал в `$fs_root$` fallback. Этот баг полностью маскировал результат первого фикса. Связанная пара — упоминается вместе как «overlay indexing bug» в апстрим-PR.

## Долгосрочно (месяцы)

- **UTF-8 как внутренний text encoding во всём движке** — сейчас движок наследует cp1251 от Windows-1.6.02 эры: локализация XML в cp1251, font tables рендерят cp1251, save names собираются в cp1251 из Lua и проходят через FS API. На POSIX-платформах это не работает: APFS/ext4/Btrfs ждут UTF-8 во всех именах файлов; SDL TextInput выдаёт UTF-8 события; getpwuid возвращает UTF-8. Сейчас мы латаем границы (`xr_cp1251_to_utf8` в alife_storage_manager + null-checks + retries), но это не решение, а размазывание сложности. Объём миграции: (1) перекодировать res/gamedata/configs/text/* в UTF-8 (sed-конвертация + re-export); (2) font tables в xrUICore переписать с byte-lookup на UTF-8 sequence decode (или подключить ImGui font); (3) Lua-биндинги перевести на UTF-8 на C++ стороне; (4) Windows-сборка получит conversion-bridge UTF-8→cp1251 ТОЛЬКО на границе с Windows ANSI API (ровно противоположный текущему направлению). 4-6 месяцев одного разработчика, **сильный upstream-кандидат** — все портеры на nix/macOS этим страдают.
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

- **[P0, ✓ verified 2026-05-21 (smoke)]** ~~Сброс настроек игры на macOS~~ — `user.ltx` персистится из overlay; clean shutdown без orphan'ов. Два сцепленных бага закрыты (`rescan_path` early-return + `_set_root` separator normalize). **Но**: smoke того же дня выявил отдельный P0 ниже про overlay child-paths — сейвы писались в bundle, не в overlay. Починено на ветке `issue-45-p0-smoke-cleanup`.
- **[P0, ✓ починено 2026-05-21 — ждёт verify в overlay-environment без симлинка]** Overlay child-paths не следовали за re-root `$app_data_root$`. `fsgame.ltx` определяет `$game_saves$`/`$screenshots$`/`$downloads$` как дети `$app_data_root$`; `FS_Path` хранит resolved string без parent-link и snapshotting на parse time. Старый `LocatorAPI.cpp:1041-1055` re-root'ил только `$logs$` и `$app_data_root$` — child'ы оставались с pre-built m_Path → саvinghи писались в `$fs_root$/_appdata_/savedgames/` (bundle), не overlay. Для launcher-flow с writable `$fs_root$` работало случайно; для DMG-flow с read-only/wiped bundle — data-loss. Фикс: после re-root parent'а пройти по `m_paths` и переподцепить любого FS_Path, чей `m_Root` совпадает с старым префиксом (см. `src/xrCore/LocatorAPI.cpp:1041-1090`). Generic walk — не хардкод 4 child'ов, безопасно для mod-конфигов с дополнительными aliases. Кандидат в апстрим — баг затрагивает любую sandboxed distribution.
- **[P2, новое — от пользователя]** UI разрешений на macOS должен показывать только то, что фактически помещается на экране. Сейчас vid_mode_token заполняется через `SDL_GetDisplayModes` — все режимы дисплея независимо от macOS-scaling (включая 2560×1600, который в windowed-режиме не влезет под user scaling). Фильтр по `SDL_GetDisplayUsableBounds` в `FillResolutionsForMonitor`. Заодно — кнопка/чекбокс «HiDPI mode» в options (toggle `SDL_WINDOW_ALLOW_HIGHDPI` — позволит игроку выбирать между «жирные пиксели быстрее» и «retina крепко но дороже»). До этого — временное решение: hide-large-res-on-macos.
- **[P0, ✓ verified 2026-05-21]** ~~Рендер в углу окна при vid_mode = native~~ + растяжение после отмены/возврата — **HighDPI viewport mismatch**. Smoke 21 мая подтвердил: `drawable=2048x1314 viewport=2048x1314 engine=2048x1314` на каждом RESET, размерности совпадают. Cancel-из-options всё ещё не возвращает разрешение в исходное — но это **другой** баг (cfg_save/tmp.ltx rollback flow); открыт отдельным issue.
- **[P0, ❌ defensive fix НЕ помог — переоткрыт 2026-05-21]** Поток `GL_INVALID_OPERATION (0x502)` остался в smoke 21 мая (lines 241-272 лога), VAO IDs **валидные** (4/14/15/16) — то есть default-VAO bind в `CHW::CreateDevice` не закрыл проблему. Корень глубже, как и предполагалось в roadmap fallback: вероятно `glVertexAttribPointer` legacy path в `set_Vertices` (Apple GL 4.1 может не репортить `ARB_vertex_attrib_binding`). Engine глушит спам после ~30 строк. Открыто отдельным issue (требует instrumentation в `set_Vertices` site).
- **[P0, диагностика добавлена] Автосейвы появляются в-сессии, но пропадают после рестарта** — на диске сегодняшних автосейвов нет вовсе (видны старые от 17 мая с cp1251-байтами в filename, новые — нет). `autosave_manager.cpp::shedule_Update` захардкожен `if (true) return;` — реальные автосейвы из Lua → `M_SAVE_GAME` → `CALifeStorageManager::save` → `FS.w_open`. Без логирования невозможно сказать, w_open возвращает null, файл создаётся не там, или filename mismatch при load. Добавлен unconditional `Msg("* Game save attempt: name=... bytes=... path='...'")` + null-check w_open (`alife_storage_manager.cpp`). После repro гипотеза станет фактом. **Возможно частично связано** с предыдущим overlay-child-paths фиксом — сейвы шли в bundle, что могло сломать atomic-rename для autosave-перезаписи.
- **[P0, ждёт repro] Crash при изменении некоторых параметров графики** — пользователь боится менять, потому что есть параметры которые крашат игру. Нужно: (а) от пользователя — список конкретных параметров, которые крашат, и stack trace из `~/Library/Logs/OpenXRay/openxray_<user>.log`; (б) идея на будущее — спрятать на macOS опции, которые мы заведомо знаем, что крашат (требует enum'а опций в UI XML и matching на стороне движка).
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

## Live-confirmed (2026-05-19, after safe-mode landed)

- **Safe-mode boot recovery works** — sentinel cleared after 120 stable
  frames on both verified launches; previous-broken state cleared; normal
  exit followed by normal restart kept user.ltx settings. The cycle is
  closed.
- **Cmd+Q in-level still triggers TX-state.** User confirmed:
  - In main menu: Cmd+Q kills cleanly, process exits.
  - On a level: Cmd+Q drives the engine into shutdown which iterates
    `glDelete*` over framebuffers/textures/buffers; each delete blocks
    on a busy GPU; compound waits go to TX-state.
  - The Cocoa Cmd+Q shim (commit `bdb603574`) was supposed to intercept
    before engine sees it -- intent was synth `SDLK_ESCAPE` to open
    pause menu -- but it was never runtime-verified. Two follow-ups:
    1. Verify the NSEvent monitor is actually installed and intercepts.
    2. If 1 fails or is unreliable: detect "in level" state and force
       `_exit(0)` from the shim, skipping engine shutdown entirely.
       Dirty but bypasses the kernel-level hang root cause.

## Live-confirmed (2026-05-19, renderer playground done)

- **Playground epic #12 closed, v0+v1+v2 promoted to stable** (`42ec77759`).
  Six tabs in `/Applications/OpenXRay.app` under F11:
  - Frame Stats — FPS / RenderFPS / TPS / polys
  - GL State — VAO / program / draw FBO / read FBO + per-frame draw counters
  - RT Picker — enumerates `m_rtargets`, ImGui::Image preview of selected
  - Event Log — last 64 GL errors via xr_gl_error_sink CHK_GL hook
  - Pipeline Toggles — shadows / occq / details / wallmarks (DebugRenderToggles)
  - Hot Reload — FSEvents watcher on `$game_shaders$` (Apple-only)
- **Cmd+Q TX-state fixed via FlushGpuQueue** (`f2b172380`). Renderer drains
  GPU command queue (glFinish on Apple) at start of OnDeviceDestroy so
  the glDelete* cascade doesn't compound mach_msg waits. Player tested:
  in-level Cmd+Q goes to pause menu → Quit → clean shutdown without
  TX-state. Stage 2 (_exit(0) on double Cmd+Q) remains as fail-safe.
- **Log rotation cap** (`960ad3043`). Logs > 100MB at boot get unlinked
  instead of rotated to .bkp — prevents 1GB log accumulation cycle.
- **Pipeline-toggle constraint observed during testing.** Shadow toggle
  flips visibility only when sun shadows are enabled in graphics settings.
  By design: the gate is downstream of the engine's own
  `RImplementation.o.oldshadowcascades` decision, so if the engine never
  built shadow maps this frame the toggle has nothing to gate. Acceptable
  for v2; if we want "override-on" we'd need an upstream gate too.

## Deferred from playground (re-evaluate if/when picked up)

- **Live in-engine shader swap** — FSEvents watcher captures changes; UI
  surfaces them; engine does NOT yet atomically re-compile and swap
  SPS::sh / SVS::sh GLuint program objects mid-session. 1-2 days of
  careful work to do right (include graph tracking, ref_ps/ref_vs
  refcounting, atomic GL program replacement). Watcher is reusable
  infrastructure for the future swap.
- **Playground v3** — GPU timer queries per pipeline stage, frame
  timeline waterfall. Deferred until concrete performance work demands
  this view.

## Live-confirmed (2026-05-19, post-epic infrastructure landed)

- **ALife Inspector v0** (`e399844d1`). Walks `ai().alife().objects().objects()`,
  categorises via `dynamic_cast` against `CSE_ALifeHumanAbstract` /
  `CSE_ALifeMonsterAbstract` / `CSE_ALifeInventoryItem` /
  `CSE_ALifeAnomalousZone`. Shows total / online / offline + per-class
  counts + current level name. Same `ide_tool` pattern as playground.
- **`ide::ToggleNamedTool(name)` dispatch** added so xrGame can open
  named ide_tool overlays without xrEngine knowing the subclass.
  Enables future dev panels without growing the IRender / ide-virtual
  surface.
- **`dev_tools` cvar gate** (`667b87594`, `195b6c152`). Runtime
  `ENGINE_API int g_dev_tools` (default 0 in MasterGold, 1 elsewhere),
  registered as `CCC_Integer "dev_tools" 0..1`. All three F-key
  dispatch sites (ide::IR_OnKeyboardPress, CLevel::IR_OnKeyboardPress,
  CMainMenu::IR_OnKeyboardPress) early-out when `g_dev_tools == 0`.
  QA enables in a shipped MasterGold via console `dev_tools 1` — no
  rebuild.
- **F6/F7 default binding for dev hotkeys.** F11 grabbed by macOS
  Mission Control; F12 grabbed by hardware Volume Up media key
  (intercepted before SDL sees it, even with "Use F1, F2... as standard
  function keys" enabled — confirmed empirically). F6/F7 reach SDL
  cleanly. `user.ltx` persists per-user overrides, so the default
  change only affects fresh installs.

## Live-confirmed (2026-05-21, in-game updater MVP)

Issue #39 closed by merge `2bdc27b71`. End-to-end smoke against
`python -m http.server` walked the four mandatory scenarios:

- Manifest fetch + version compare against `xrCore::GetForkVersion()`
- "Доступно обновление. Загрузить сейчас?" dialog (`msg_box_new_patch`)
- Asset download into `$app_data_root$/updates/pending.app.zip`
- "Применить при следующем запуске?" dialog (`msg_box_patch_download_success`)

What landed:

- **`XRAY_FORK_VERSION` plumbing** (`faf94034b`). CMake `set(... CACHE
  STRING)` → `target_compile_definitions` → `xrCore::buildForkVersion`,
  printed on engine init next to the upstream build ID. Default
  `1.6.fork.dev`; override at release time with `-DXRAY_FORK_VERSION=...`.
- **`CGameSpy_HTTP::FetchString`** (`119feb7af`). New in-memory GET
  variant for the manifest, mirrors the existing `DownloadFile` API
  shape via `fastdelegate`. Same commit fixes a pre-existing
  use-after-free on `DownloadContext` (was stack-allocated, ghttp
  invokes callbacks asynchronously after the stack frame is gone) —
  candidate upstream PR.
- **`ParseUpdateManifest`** (`9313db07d` then rewritten in `08e6d9725`).
  First pass tried `CInifile` over a memory buffer; failed because
  CInifile treats `//` as a comment marker and silently truncates
  `asset_url = http://...` to `http:`. Final version is a 60-line
  hand-rolled key=value scanner — single `;`/`#` line comment, no
  `//` special-casing.
- **Updater UI integration** (`8b6c28810`, refined by `c88087dff`).
  `CMainMenu::TriggerUpdateCheck` kicks the fetch; three new
  `EErrorDlg` entries (`NewPatch`, `PatchDownloadError`,
  `PatchDownloadSuccess`) reuse XML templates that vanilla shipped but
  never wired up. Strings live in our `openxray.xml` so we control the
  text without touching upstream localisation files.
- **Path plumbing across engine FS / ghttp boundary** (`715820e35`,
  `1012d4ffe`, `b28433e7f`). Three iterations to land the right
  recipe: pass suffix with `\\` so the assembled path is uniformly
  engine-style → `VerifyPath` to mkdir each segment up to the file →
  `convert_path_separators` for ghttp's POSIX fopen. The bug that took
  longest to nail was VerifyPath's "mkdirs every segment before a
  separator but not the trailing one"; with a `/` before `updates`,
  the directory itself never got created.
- **`updater_manifest_url` console cvar.** Tunable from console, no
  rebuild needed when the intranet host appears.
- **Smoke recipe doc** (`notes/updater-smoke-RU.md`,
  `notes/updater-smoke.md`). Nine scenarios with exact shell commands.

Follow-ups filed and not blocking:

- #41 SHA256 verification (CommonCrypto on macOS — manifest field is
  read but unenforced today)
- #42 Atomic `.app` swap on next launch
- #43 `cancel_download` console cmd / Cancel button

## Deferred from ALife Inspector v0 (next-layer features)

- Per-object detail panel — select an NPC in the list → its task,
  faction, position, inventory contents. Needs a selection model
  beyond aggregate counters.
- Smart-terrain task state visualisation — which terrain runs which
  scripted task, current job slot occupancy.
- Faction inventory drift tracking over time — sample on a timer,
  diff against baseline.
- NPC dialog memory tied to player reputation — needs schema dump
  from the ALife dialog system, currently opaque to the inspector.
- Open-world adaptation (semi-/full-open). Deferred until vanilla CoP
  is end-to-end stable on macOS.
