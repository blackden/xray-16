# OpenXRay: сборка на macOS ARM64

Инструкция предназначена только для Apple Silicon. Все шаги предполагают архитектуру ARM64 и не формируют универсальные бинарники.

## Требования
- macOS 13 Ventura или новее на Apple Silicon.
- Установленные инструменты командной строки Xcode: `xcode-select --install`.
- Homebrew в `/opt/homebrew`.
- Клон репозитория с подмодулями:  
  ```bash
  git clone https://github.com/blackden/xray-16.git --recurse-submodules
  cd xray-16
  ```

## Первичная подготовка
Установите зависимости и настройте окружение:

```bash
./scripts/bootstrap-macos.sh
```

Скрипт проверит наличие Xcode CLT, запустит `brew bundle` и сохранит рекомендуемое значение `CMAKE_BUILD_PARALLEL_LEVEL`, рассчитанное по количеству ядер.

## Сборка
Используйте обёртку (по умолчанию собирается `RelWithDebInfo`):

```bash
./scripts/build.sh                    # RelWithDebInfo
./scripts/build.sh Debug              # Debug
./scripts/build.sh Release            # Release
```

Готовые бинарники появляются в `build/macos-arm64/<Config>/`. Пресеты используют Ninja и учитывают `CMAKE_BUILD_PARALLEL_LEVEL`, поэтому значение по умолчанию (`sysctl -n hw.ncpu` из bootstrap) эквивалентно `make -j$(sysctl -n hw.ncpu)`. При необходимости можно обратиться напрямую к пресетам CMake: `cmake --preset macos-arm64-relwithdebinfo` и `cmake --build --preset macos-arm64-relwithdebinfo-build`.

## Игровые данные и запуск
1. Сохраните каталог `res/` из репозитория. При необходимости выведите его рядом со сборкой символической ссылкой, а не копированием:
   ```bash
   CONFIG=RelWithDebInfo
   BUILD_DIR="${PWD}/build/macos-arm64/${CONFIG}"
   ln -sfn "${PWD}/res" "${BUILD_DIR}/res"
   ```
2. Разместите ресурсы версии игры для Windows (каталоги `gamedata`, `.db*`, `levels`, `localization`, `resources` и т.п.) рядом с `res/fsgame.ltx`. Можно скопировать файлы или создать символические ссылки. Шейдеры из этого репозитория уже лежат в `res/gamedata/shaders/`.
3. Запустите движок (из любой директории):
   ```bash
   "${BUILD_DIR}/bin/xr_3da" -fsltx "${PWD}/res/fsgame.ltx" -renderer gl4
   ```
   На macOS следует использовать `-renderer gl4`; другие режимы обычно завершаются ошибкой «Can't setup renderer».

## Логи и диагностика
- Каждый вспомогательный скрипт сохраняет лог с меткой времени в каталоге `logs/` (например, `logs/build_YYYYMMDD_HHMMSS.log`). Последний лог сборки доступен по симлинку `logs/latest_build.log`.
- Можно временно отключить логирование, установив `NO_LOG=1`.
- После конфигурации копии `CMakeOutput.log` и `CMakeError.log` (если появились) автоматически помещаются в `logs/` рядом с логом сборки.
- Скрипт `ci/local-build.sh` последовательно выполняет bootstrap и сборку, сохраняя отдельный лог.
- Дополнительный помощник для запуска: `scripts/run-with-log.sh --config RelWithDebInfo -- -fsltx ./res/fsgame.ltx -renderer gl4` добавляет флаг `-log` и копирует файлы `xray_*.log` в `logs/run_<timestamp>/`.

## Отладка
- Для получения бэктрейса воспользуйтесь LLDB:
  ```bash
  lldb -- "${PWD}/build/macos-arm64/RelWithDebInfo/bin/xr_3da" --args -fsltx "${PWD}/res/fsgame.ltx" -renderer gl4
  ```
- Убедитесь, что `brew bundle` завершился без ошибок; отсутствие библиотек приведёт к проблемам конфигурации CMake.
- При необходимости полной пересборки удалите каталог `build/macos-arm64/`.
- Сообщение «Can't setup renderer» чаще всего означает, что путь к данным указан неверно. Проверьте аргумент `-fsltx` и наличие игровых архивов в `res/`.

## Политика по архитектуре
Только ARM64. Поддержка x86_64, Rosetta и universal-билдов в рамках этого пайплайна не рассматривается.
