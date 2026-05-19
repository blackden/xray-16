# OpenXRay macOS — Cheatsheet

Живой документ. Сюда добавляются повторяющиеся вопросы и команды,
чтобы не искать их в транскриптах. Если что-то спрашиваешь второй раз —
оно должно оказаться здесь.

## Запуск игры

```bash
# Самое простое — shipped DMG, путь к gamedata зашит в launcher:
open '/Applications/OpenXRay-Dev.app'

# Через make — собирает из исходников и запускает с указанным fsgame.ltx:
make run FSGAME_LTX=/Users/ragnar/Games/STALKER-CoP/fsgame.ltx

# Переопределить gamedata-путь без правки fsgame.ltx (overlay):
OPENXRAY_FSGAME_LTX=/path/to/other/fsgame.ltx open '/Applications/OpenXRay-Dev.app'

# Дебаг-сессия в lldb (бэктрейс при крэше):
make run-lldb FSGAME_LTX=/Users/ragnar/Games/STALKER-CoP/fsgame.ltx
```

## Скачивание игры и переключение языка

Steam-аккаунт должен владеть игрой. `steamcmd` ставится через
`brew install --cask steamcmd`.

```bash
# По умолчанию — только английский voice/text:
make install-game STEAM_LOGIN=<твой_steam_логин>

# С докачкой русского voice + text (~700 MB поверх английского):
make install-game STEAM_LOGIN=<твой_steam_логин> LANGUAGE=russian

# Любой другой язык:
make install-game STEAM_LOGIN=<твой_steam_логин> LANGUAGE=french
# Valid: english, russian, french, german, italian, spanish.
```

После докачки в `localization/` появится `x<language>.db`
(`xrussian.db`, `xfrench.db`, ...). Английский `xenglish.db` остаётся
на месте — переключение делается в `user.ltx`/консоли, не перекачкой.

**Чтобы переключить язык в уже установленной игре:**

```ini
# /Users/ragnar/Library/Application Support/OpenXRay/_appdata_/user.ltx
g_language rus      # eng, rus, fre, ger, ita, spa
```

В консоли игры (после правки или для смены без перезапуска):
```
g_language rus
snd_restart
```

`snd_restart` перезагружает sound subsystem с новой `.db`. Без него
тексты переключатся, а voice — нет.

## Биндинги клавиш

**Изменить bind через консоль** (откройте консоль клавишей `~`):

```
bind pause kP            # пауза → P (на маке нет физической Pause)
bind <action> <key>      # общий синтаксис
unbindall                # сброс к дефолту
```

Сохраняется автоматически в `user.ltx`.

**Источник истины по именам клавиш:**
`src/xrEngine/xr_level_controller.cpp:225+` — таблица `keyboards[]`.

**Mac-friendly клавиши (работают без Fn):**

| Категория | Имена |
|-----------|-------|
| Буквы | `kA` .. `kZ` |
| Цифры | `k0` .. `k9` |
| F-клавиши | `kF1`, `kF2`, `kF5`–`kF10` |
| Навигация | `kSPACE`, `kRETURN`, `kESCAPE`, `kTAB`, `kBACKSPACE`, `kDELETE` |
| Стрелки | `kLEFT`, `kRIGHT`, `kUP`, `kDOWN` |
| Pg/Home | `kHOME`, `kEND`, `kPGUP`, `kPGDN`, `kINSERT` |
| Спецсимволы | `kGRAVE` (`/~), `kLBRACKET`, `kRBRACKET`, `kAPOSTROPHE`, `kSEMICOLON`, `kCOMMA`, `kPERIOD`, `kSLASH`, `kBACKSLASH`, `kMINUS`, `kEQUALS` |
| Модификаторы | `kLCONTROL`, `kRCONTROL`, `kLSHIFT`, `kRSHIFT`, `kLMENU` (LAlt), `kRMENU` (RAlt), `kLWIN`/`kRWIN` (Cmd) |
| Мышь | `mouse1`, `mouse2`, `mouse3`, `mouse4`, `mouse5` |

**Чего НЕТ на MacBook без внешней клавы** (не биндить):
`kPAUSE`, `kPRINTSCREEN`, `kSCROLL`, `kNUMLOCK`, `kNUMPAD*`.

**Отжато macOS по умолчанию** (надо снять в System Settings → Keyboard,
если нужно использовать):
- `kF3` — Mission Control
- `kF4` — Launchpad
- `kF11` — Show Desktop
- `kF12` — Notification Center
- `kLWIN`/`kRWIN` — Cmd используется системой (Cmd+Tab, Cmd+Q и т.д.)

**Рекомендации для типичных переменных:**
- Пауза → `kP` (мнемоника) или `kBACKSPACE`.
- Renderer Playground / ALife Inspector — `kF6` / `kF7` (дефолт).
- Скриншот — `kF8` (свободно).

## Полезные console commands

Консоль открывается `~` (`kGRAVE`).

| Команда | Что делает |
|---------|------------|
| `dev_tools 1` | Включает dev-хоткеи F6/F7 в shipped билде (в MasterGold по умолчанию выключены) |
| `g_language rus` | Сменить язык на русский (нужен `xrussian.db`) |
| `snd_restart` | Перезагрузить sound subsystem (нужно после смены языка) |
| `bind <action> <kKEY>` | Назначить клавишу действию (автосохраняется) |
| `unbindall` | Сбросить все биндинги |
| `flush` | Сбросить лог-буфер на диск (без перезапуска) |
| `help` | Список всех консольных команд |
| `quit` | Выйти из игры |

## Локации файлов

| Что | Где |
|-----|-----|
| Игровые данные (gamedata) | `/Users/ragnar/Games/STALKER-CoP/` |
| `fsgame.ltx` | `/Users/ragnar/Games/STALKER-CoP/fsgame.ltx` |
| Озвучка (.db архивы) | `/Users/ragnar/Games/STALKER-CoP/localization/x<language>.db` |
| `user.ltx` (биндинги + cvars) | `/Users/ragnar/Library/Application Support/OpenXRay/_appdata_/user.ltx` |
| Сейвы | `/Users/ragnar/Library/Application Support/OpenXRay/_appdata_/savedgames/` |
| Логи | `/Users/ragnar/Library/Logs/OpenXRay/openxray.log` |
| Установленный launcher (auto-shipped) | `/Applications/OpenXRay-Dev.app` |
| Stable launcher | `/Applications/OpenXRay.app` |

## Анализ логов

```bash
# Самый свежий лог:
tail -f ~/Library/Logs/OpenXRay/openxray.log

# Где тормозит загрузка (по фазам, из недавнего патча):
grep '\[load\] phase' ~/Library/Logs/OpenXRay/openxray.log | tail -30

# Все GL errors:
grep -i 'gl error\|0x05' ~/Library/Logs/OpenXRay/openxray.log

# Что движок думал про язык / звуки:
grep -iE 'language|snd_|sound.*db' ~/Library/Logs/OpenXRay/openxray.log
```

## Полезные `make`-таргеты

```bash
make help                    # список всех таргетов + текущие переменные
make build                   # cmake configure + build (debug)
make build-release           # ReleaseMasterGold (то, что уходит в DMG)
make ship                    # build-release + install в /Applications/OpenXRay-Dev.app
make promote                 # отметить текущий HEAD как stable
make all-in-one              # собрать DMG (.app + gamedata side-by-side, ~3.5GB)
make run                     # запустить с FSGAME_LTX
make run-lldb                # запустить под lldb (бэктрейс на крэш)
make test                    # регрешн-тесты + UTF-8/cp1251 characterization
make install-game            # скачать игру через steamcmd (см. выше)
make install-hooks           # подключить auto-ship на каждом коммите
```

## Куда складывать новые повторяющиеся вопросы

Если ты задаёшь вопрос **второй раз** — добавь ответ сюда, в подходящий
раздел. Если темы нет — заведи новую секцию. Цель — чтобы любой
"типичный вопрос про OpenXRay на маке" находился grep'ом по этому файлу
за 5 секунд.
