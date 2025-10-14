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

Готовые бинарники появляются в `build/macos-arm64/<Config>/`. При необходимости можно обратиться напрямую к пресетам CMake: `cmake --preset macos-arm64-relwithdebinfo` и `cmake --build --preset macos-arm64-relwithdebinfo-build`.

## Портативный запуск
1. Скопируйте ресурсы движка рядом с бинарями:
   ```bash
   CONFIG=RelWithDebInfo
   BUILD_DIR="build/macos-arm64/${CONFIG}"
   cp -R res "${BUILD_DIR}/"
   ```
2. Поместите ресурсы игры (каталог `gamedata`, файлы `.db*`) рядом с `res/fsgame.ltx`.
3. Запустите движок из каталога сборки:
   ```bash
   cd "${BUILD_DIR}/bin"
   ./xr_3da -fsltx "${BUILD_DIR}/res/fsgame.ltx" -renderer gl4
   ```

## Отладка
- Для получения бэктрейса воспользуйтесь LLDB:
  ```bash
  lldb -- "${PWD}/build/macos-arm64/RelWithDebInfo/bin/xr_3da" --args -fsltx "${PWD}/build/macos-arm64/RelWithDebInfo/res/fsgame.ltx" -renderer gl4
  ```
- Убедитесь, что `brew bundle` завершился без ошибок; отсутствие библиотек приведёт к проблемам конфигурации CMake.
- При необходимости полной пересборки удалите каталог `build/macos-arm64/`.

## Политика по архитектуре
Только ARM64. Поддержка x86_64, Rosetta и universal-билдов в рамках этого пайплайна не рассматривается.
