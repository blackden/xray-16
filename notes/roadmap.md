# Roadmap

Открытые задачи в порядке убывания пользы для нашей цели (поиграть на macOS, в идеале на максимальных настройках, нативно).

## Краткосрочно (часы-дни)

- **Упаковка нашей сборки в `.app` бандл** — Info.plist, copy dylibs, install_name_tool rpath, codesign. Шаблон взят у [Mac Source Ports build script](https://github.com/MacSourcePorts/MSPBuildSystem/blob/main/xray-16/macsourceports_arm64.sh). **Готово**: `make package` → `dist/OpenXRay.app` с иконкой Monolith и launcher-shim'ом. См. [notes/macos-build-guide.md](macos-build-guide.md).
- **Локализация мелочей в `res/gamedata/configs/text/rus/openxray.xml`** — "Выйти в Windows" → "Выйти в macOS" (частично сделано; работает в Clear Sky UI style, в дефолтном CoP menu пока остаётся). Подробности в "Известные баги".
- **DMG для брата с бандленными игровыми данными** — поверх `dist/OpenXRay.app` положить game data файлы (`db/`, `localization/`, `levels/`, `resources/`, etc.) и фиксированный `fsgame.ltx` рядом. Идея: `dist/OpenXRay.app` + `dist/STALKER-CoP/` в одной DMG → пользователь перетаскивает оба в `/Applications/`, launcher знает где искать. Альтернатива: всё внутри `.app/Contents/Resources/STALKER-CoP/` — но это распухает бандл до ~5 GB. Дизайн обсуждается.
- **Cocoa intervention (Plan B)** — `disableAutomaticTermination` + override `applicationShouldTerminateAfterLastWindowClosed`. Закрывает один из путей смерти на macOS. ~30 минут ObjC.

## Среднесрочно (недели)

- **Multi-frame для других hot spot'ов**:
  - Lua script bulk loading (~70 скриптов подряд)
  - Spawn registry / AI space load
  - Texture load в options menu
  Сейчас живёт за счёт того, что Release-сборка не вылетает на debug-asserts; но в любой момент может стрельнуть hang detection.
- **Кириллица в saves** — выяснить точный сценарий (см. отдельный пункт ниже).
- **Зафиксить настоящий buffer overflow в `xrDebug::Fail`** — PAC trap во время FATAL dialog на macOS. Возможно полезно апстрим даже на других платформах.

## Долгосрочно (месяцы)

- **Новый рендер: Metal или Vulkan через MoltenVK** — единственный путь к "max settings" без glitch'ей света и без compatibility-патчей. Сейчас Apple OpenGL 4.1 (Metal-backed) — это shim с дырами:
  - Compressed 3D textures не поддерживаются → water_sbumpvolume FATAL
  - MSAA framebuffer attachments кривые → set_RT FATAL
  - Light glitches зависящие от ракурса (наблюдается)
  - Низкий теоретический потолок производительности
  Объём: 2-4 месяца на Vulkan backend, 3-6 на Metal. Vulkan приоритетнее — польза и Linux, и шанс попасть в апстрим.

## Известные открытые баги

- **"Выйти в Windows" в дефолтном CoP main menu** — наш overlay `ui_mm_quit2windows="Выйти в macOS"` срабатывает только для Clear Sky UI style (`styles_/ui_style_cs/`). Дефолтный CoP main menu (`ui_mm_main.xml`) живёт внутри `.db*` архивов и использует другой string id для quit-кнопки и подтверждающего диалога (`message_box_quit_windows`). Чинится одним из двух:
  - Распаковать `.db` (упаковщик `src/utils/xrCompress`, распаковщика готового нет — нужно либо использовать сторонний `db_unpacker`, либо проинструментировать движок чтобы он дампил), найти string id, добавить в `res/gamedata/configs/text/rus/openxray.xml`
  - Положить полный overlay `res/gamedata/configs/ui/ui_mm_main.xml` с нашим caption — но это копия 100+ строк, придётся синхронизировать с апстримом
- **Light glitches** на Скадовске — зависит от угла камеры. Корень — Apple GL shim.
- **Cmd+Q крашит** (наследие MSP build известная проблема, наш build тоже подвержен) — это PAC trap в xrDebug::Fail когда движок инициирует terminate.

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
