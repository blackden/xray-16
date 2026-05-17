# OpenXRay on macOS — build & run guide

Воспроизводимая инструкция: как привести этот форк к работающему `.app`-бандлу на Apple Silicon и Intel Mac, со всеми фиксами из ветки `macos/blackden/master`.

## Что получится

`dist/OpenXRay.app` — самодостаточный бандл:
- Один бинарь (`xr_3da`, статически слинкованный с движком в ReleaseMasterGold)
- 7 рантайм-дилибов из Homebrew (SDL2, libogg, libvorbis, libvorbisfile, libjpeg, liblzo2, libtheora) — внутри бандла, не нужны на машине пользователя
- Иконка Monolith
- Launcher-shim, пишущий лог в `~/Library/Logs/OpenXRay/`
- Ad-hoc codesign (Gatekeeper при первом запуске может потребовать "ПКМ → Открыть")

Размер: ~80 MB.

## Предусловия (на машине разработчика)

```bash
xcode-select --install                # Command Line Tools
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"  # Homebrew
brew bundle install --file=Brewfile   # cmake, sdl2, libogg, libvorbis, jpeg-turbo, lzo, theora, etc.
git submodule update --init --recursive
```

`make setup` делает всё то же самое одной командой.

## Установка игровых данных

```bash
make install-game STEAM_LOGIN=ваш_steam_логин          # ставит CoP через steamcmd в ~/Games/STALKER-CoP
make link-gamedata                                      # симлинк res/gamedata -> ~/Games/STALKER-CoP/gamedata
```

`link-gamedata` нужен — в `res/gamedata/` лежат OpenGL-шейдеры и OpenXRay-овские строковые overlay'и (включая "Выйти в macOS"). Без него движок не найдёт GL-шейдеры и упадёт.

После этого:
```
~/Games/STALKER-CoP/
├── gamedata -> /Users/.../xray-16/res/gamedata    (наш симлинк, GL шейдеры + overlay'и)
├── db/                                            (CoP .db архивы из Steam)
├── localization/
├── levels/
├── fsgame.ltx                                     (root paths, читается движком)
└── _appdata_/                                     (saves, user.ltx, логи движка)
```

## Сборка

Две конфигурации параллельно живут:

```bash
make build                          # BUILD_TYPE=Mixed (DEBUG, для разработки) -> bin/<arch>/Mixed/
make build BUILD_TYPE=ReleaseMasterGold BUILD_DIR=build-release    # релизный, монолитный xr_3da -> bin/<arch>/ReleaseMasterGold/
```

В Mixed билде VERIFY/CHK_GL активны — FATAL на любую GL-ошибку. В ReleaseMasterGold макросы — no-op, движок переживает обычные Apple-GL шероховатости (compressed 3D textures, MSAA framebuffer attachments) с визуальными артефактами вместо краша.

## Запуск (для разработки)

```bash
make run        # Mixed билд, под капотом захватывает stdout/crash report в notes/session-YYYYMMDD-HHMMSS/
make run-lldb   # то же, под lldb с автоматическим bt all при крахе
```

## Упаковка в `.app`

```bash
make package
```

Это:
1. Собирает `ReleaseMasterGold` в `build-release/` → `bin/<arch>/ReleaseMasterGold/xr_3da`
2. Создаёт `dist/OpenXRay.app/`:
   - `Contents/MacOS/xr_3da` — бинарь
   - `Contents/MacOS/OpenXRay` — bash launcher, `exec`-ает xr_3da с `-fsltx ${OPENXRAY_FSGAME_LTX:-$HOME/Games/STALKER-CoP/fsgame.ltx} -nointro`, логирует в `~/Library/Logs/OpenXRay/openxray.log`
   - `Contents/MacOS/*.dylib` — копии Homebrew зависимостей с `@rpath`-ссылками (transitive closure через `otool -L`)
   - `Contents/Resources/openxray.icns` — иконка
   - `Contents/Info.plist`, `Contents/PkgInfo`, `Contents/_CodeSignature/*`
3. Ad-hoc подписывает (`codesign --force --deep --sign -`)

Скрипт упаковки: [scripts/mac/package_app.sh](../scripts/mac/package_app.sh). Игровая иконка: [scripts/mac/openxray.icns](../scripts/mac/openxray.icns) (сделана из 256×256 webp через `sips` + `iconutil`).

## Запуск собранного `.app`

```bash
open dist/OpenXRay.app
# или из Finder двойной клик
# первый раз Gatekeeper может ругнуться → ПКМ → Открыть → Открыть
```

Чтобы указать другой путь к `fsgame.ltx`:
```bash
OPENXRAY_FSGAME_LTX=/path/to/fsgame.ltx open dist/OpenXRay.app
```

Логи: `~/Library/Logs/OpenXRay/openxray.log`. Engine собственные логи (включая `_appdata_/logs/`): внутри `GAME_DIR/_appdata_/logs/`.

### Запуск с отладкой

Если игра падает или зависает, и хочется собрать stack trace для отчёта:

```bash
open dist/OpenXRay.app --args --debug
# или напрямую:
/Applications/OpenXRay.app/Contents/MacOS/OpenXRay --debug
```

Launcher детектирует флаг `--debug`, проверяет наличие `lldb` в `PATH` и запускает `xr_3da` под отладчиком. Любой crash → полный `thread backtrace all` уходит в `~/Library/Logs/OpenXRay/openxray-debug.log`.

Требования:
- На машине должен быть установлен `lldb`. На macOS он входит в **Xcode Command Line Tools** — установка одной командой:
  ```bash
  xcode-select --install
  ```
- Бандл подписан с entitlement `com.apple.security.get-task-allow` (см. [scripts/mac/debug.entitlements](../scripts/mac/debug.entitlements)) — без неё macOS блокирует attach даже к ad-hoc подписанному бинарю.

Если `lldb` не найден, launcher пишет предупреждение в `openxray-debug.log` и запускает обычно, без отладчика.

## Распространение брату/другому Mac'у

**Бандл самодостаточен по библиотекам**. Homebrew у получателя не требуется.

Нужны только:
1. macOS 11+ (Big Sur и выше)
2. Apple Silicon ИЛИ Intel x86_64 (нужен билд под нужную архитектуру; кросс-сборка `-DCMAKE_OSX_ARCHITECTURES=x86_64` на Apple Silicon работает)
3. Игровые данные — либо лицензионный CoP из Steam + `make install-game` на машине получателя, либо см. `make all-in-one` ниже.

### Вариант "всё в одном" — `make all-in-one`

Для случая, когда получателю не нужно ни steamcmd, ни `make`, ни понимания структуры игры:

```bash
make all-in-one                                       # использует $GAME_DIR (~/Games/STALKER-CoP)
make all-in-one GAMEDATA_SRC=/path/to/STALKER-CoP     # либо явно указать источник
```

Результат — `dist/OpenXRay-AllInOne.dmg` (~4.5 ГБ, UDZO zlib compression). Внутри:
- `OpenXRay.app` — наш бандл движка с дилибами.
- `STALKER-CoP/` — игровые данные: `gamedata/` (дереференс симлинка на `res/gamedata/`), `levels/`, `localization/`, `mp/`, `patches/`, `resources/`, `fsgame.ltx`, `user.ltx`.
- `README.txt` — короткая инструкция по-русски.

**НЕ попадает в DMG**: Windows-only хлам (`bin/`, `directx/`, `Stalker-COP.exe`, `helpers/`, `steamapps/`, `41700_install.vdf`, `ReadMe.txt`) и `_appdata_/` (саваам — туда же, куда движок будет писать новые).

**Куда уходят сейвы и логи получателя:** `~/Library/Application Support/OpenXRay/_appdata_/`. Лаунчер передаёт `-overlaypath ~/.openxray-data/` в xr_3da (это симлинк на Application Support, нужен из-за того что `-overlaypath` парсится `sscanf %[^ ]` и не любит пробелов). Папка `STALKER-CoP/_appdata_/` внутри установки остаётся пустой/отсутствующей.

**Шаги получателю:**
```bash
# 1. Скачать и смонтировать DMG (двойной клик в Finder тоже работает)
hdiutil attach OpenXRay-AllInOne.dmg

# 2. Перетащить ОБЕ иконки в /Applications/ (или ~/Applications/)
cp -R "/Volumes/OpenXRay All-in-One/OpenXRay.app" /Applications/
cp -R "/Volumes/OpenXRay All-in-One/STALKER-CoP"  /Applications/

# 3. Размонтировать DMG, запустить
hdiutil detach "/Volumes/OpenXRay All-in-One"
open /Applications/OpenXRay.app
```

При первом запуске Gatekeeper может ругнуться (ad-hoc подпись): ПКМ → Открыть → Открыть. Лаунчер ищет `fsgame.ltx` каскадно: `$OPENXRAY_FSGAME_LTX` → рядом с `.app` (для запуска прямо с DMG) → `/Applications/STALKER-CoP/` → `~/Applications/STALKER-CoP/` → забитый при сборке дефолт. Если ни одного нет — показывает алёрт с инструкцией.

### Вариант "без бандленных данных" — `make package`

Старый сценарий: только `.app`, ~80 МБ, получатель сам ставит игру.

```bash
# 1. Установить .app
hdiutil attach OpenXRay.dmg && cp -R "/Volumes/OpenXRay/OpenXRay.app" /Applications/

# 2. Игровые данные (если не бандлили):
mkdir -p ~/Games/STALKER-CoP && unzip stalker-cop-data.zip -d ~/Games/STALKER-CoP/

# 3. Запустить
open /Applications/OpenXRay.app
```

## Команды на будущее

```bash
make clean                          # снести build/, build-release/, bin/, dist/
make rebuild                        # clean + build
make help                           # все цели + текущие переменные
```

Переменные через окружение или `.env`:
- `BUILD_TYPE` — `Mixed` (по умолч.), `Release`, `ReleaseMasterGold`, `Debug`
- `BUILD_DIR` — папка cmake-кэша (по умолч. `build`)
- `GAME_DIR` — где steamcmd ставит игру (по умолч. `~/Games/STALKER-CoP`)
- `GAMEDATA_SRC` — откуда `make all-in-one` берёт игровые данные (по умолч. `$GAME_DIR`)
- `FSGAME_LTX` — путь к fsgame.ltx (по умолч. `$GAME_DIR/fsgame.ltx`)
- `PARALLEL` — число параллельных задач сборки (по умолч. 4)

## Что внутри (наши коммиты)

См. [notes/roadmap.md](roadmap.md). Ключевое:
- Multi-frame prefetch — главный фикс silent kill во время загрузки уровня (`CResourceManager::DeferredUploadBegin/Step` + `IGame_Persistent::Prefetch_Tick`)
- CHK_GL log-and-continue на Apple — другие GL-операции не валят процесс
- `xr_utf8_to_cp1251` хелпер + использование в save-уведомлении — кириллическое имя пользователя не превращается в крокозябры
- "Выйти в macOS" (overlay строки `ui_mm_quit2windows` — пока ловится только для Clear Sky UI style; для дефолтного CoP main menu нужен XML overlay из .db архивов, в roadmap)
- VERIFY → log conversion в `glTexture.cpp` и `GameSpy_ATLAS.cpp` (антипаттерн "VERIFY + Msg-в-`if`")

## Известные ограничения

- **macOS hang detection** иногда стреляет в моменты длинных синхронных операций (Lua script bulk loading, AI space load, options menu textures). Текущий ReleaseMasterGold билд более устойчив (нет FATAL-диалогов которые случайно служили event-pump'ом раньше).
- **Cmd+Q крашит** — PAC trap в `xrDebug::Fail` (buffer overflow в форматтере). Выходить через меню.
- **Light glitches** в зависимости от ракурса камеры — Apple GL→Metal shim. Лечится только новым рендером (Metal/Vulkan, roadmap).
- **"Выйти в Windows"** в главном меню CoP остаётся — наш overlay ловится только в Clear Sky UI стиле.
