#!/usr/bin/env bash
# Build an "all-in-one" DMG: OpenXRay.app + STALKER-CoP/ game data side-by-side.
# Receiver mounts the DMG and drags both into /Applications/ — no steamcmd, no
# CLI work, just double-click.
#
# Inputs (env, with defaults):
#   GAMEDATA_SRC   Directory containing fsgame.ltx + game data (default:
#                  $HOME/Games/STALKER-CoP)
#   HOST_ARCH      arm64 | x86_64 (default: $(uname -m))
#   BUILD_TYPE     CMake config of the built engine (default: ReleaseMasterGold)
#   APP_VERSION    Version string passed through to package_app.sh (default: dev)
#
# Output: dist/OpenXRay-AllInOne.dmg

set -euo pipefail

GAMEDATA_SRC="${GAMEDATA_SRC:-${HOME}/Games/STALKER-CoP}"
HOST_ARCH="${HOST_ARCH:-$(uname -m)}"
BUILD_TYPE="${BUILD_TYPE:-ReleaseMasterGold}"
APP_VERSION="${APP_VERSION:-dev}"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DIST_DIR="${REPO_ROOT}/dist"
STAGE_DIR="${DIST_DIR}/all-in-one"
DMG_PATH="${DIST_DIR}/OpenXRay-AllInOne.dmg"
VOL_NAME="OpenXRay All-in-One"

# --- 1. Validate the game data source --------------------------------------

if [ ! -f "${GAMEDATA_SRC}/fsgame.ltx" ]; then
    echo "ERROR: ${GAMEDATA_SRC}/fsgame.ltx not found." >&2
    echo "       Pass GAMEDATA_SRC=/path/to/STALKER-CoP," >&2
    echo "       or run 'make install-game STEAM_LOGIN=...' first." >&2
    exit 1
fi
for required in levels resources localization; do
    if [ ! -d "${GAMEDATA_SRC}/${required}" ]; then
        echo "ERROR: ${GAMEDATA_SRC}/${required}/ is missing — игра не полностью установлена?" >&2
        exit 1
    fi
done

# --- 2. Build the .app bundle into the staging dir --------------------------

echo "==> Cleaning ${STAGE_DIR}"
rm -rf "${STAGE_DIR}"
mkdir -p "${STAGE_DIR}"

echo "==> Building OpenXRay.app via scripts/mac/package_app.sh"
BUILD_TYPE="${BUILD_TYPE}" \
HOST_ARCH="${HOST_ARCH}" \
APP_VERSION="${APP_VERSION}" \
DEFAULT_FSGAME_LTX="/Applications/STALKER-CoP/fsgame.ltx" \
    "${REPO_ROOT}/scripts/mac/package_app.sh"

if [ ! -d "${DIST_DIR}/OpenXRay.app" ]; then
    echo "ERROR: ${DIST_DIR}/OpenXRay.app not produced by package_app.sh" >&2
    exit 1
fi
echo "==> Moving OpenXRay.app into staging dir"
mv "${DIST_DIR}/OpenXRay.app" "${STAGE_DIR}/OpenXRay.app"

# --- 3. Copy game data subset -----------------------------------------------

GAME_OUT="${STAGE_DIR}/STALKER-CoP"
mkdir -p "${GAME_OUT}"

# Files at the root we want.
echo "==> Copying root files (fsgame.ltx, user.ltx)"
cp "${GAMEDATA_SRC}/fsgame.ltx" "${GAME_OUT}/fsgame.ltx"
if [ -f "${GAMEDATA_SRC}/user.ltx" ]; then
    cp "${GAMEDATA_SRC}/user.ltx" "${GAME_OUT}/user.ltx"
fi

# Subdirectories. gamedata/ is often a symlink to repo's res/gamedata — use -L
# (rsync -L) so we copy the real content into the bundle.
copy_dir() {
    local name="$1"
    local src="${GAMEDATA_SRC}/${name}"
    if [ ! -e "${src}" ]; then
        echo "  (skip: ${name}/ not present)"
        return
    fi
    local size
    size=$(du -sh "${src}" 2>/dev/null | awk '{print $1}')
    echo "  ${name}/  (${size})"
    # -a archive, -L deref symlinks, --info=progress2 nice for big dirs.
    rsync -aL --info=progress2 "${src}/" "${GAME_OUT}/${name}/"
}

echo "==> Copying game data subset to ${GAME_OUT}"
for d in gamedata levels localization mp patches resources; do
    copy_dir "$d"
done

# Make sure no _appdata_ ended up bundled (it shouldn't — we never copy it).
if [ -e "${GAME_OUT}/_appdata_" ]; then
    echo "==> Removing stray ${GAME_OUT}/_appdata_ (saves go to ~/Library/Application Support/OpenXRay/)"
    rm -rf "${GAME_OUT}/_appdata_"
fi

# --- 4. README.txt for the receiver -----------------------------------------

echo "==> Writing README.txt"
cat > "${STAGE_DIR}/README.txt" <<'EOF'
OpenXRay All-in-One для macOS
==============================

Что внутри:
  • OpenXRay.app    — движок (наш форк X-Ray 1.6.02)
  • STALKER-CoP/    — игровые данные (Call of Pripyat)

Установка (нужно сделать один раз):
  1. Перетащите ОБЕ иконки в /Applications/
       — OpenXRay.app
       — STALKER-CoP
  2. Кликните по OpenXRay.app
       — Первый запуск (если DMG пришёл через браузер/мессенджер): macOS
         может сказать "приложение от неизвестного разработчика". Кликните
         ПКМ → Открыть → Открыть. Дальше — обычный двойной клик.
       — При передаче через AirDrop / iCloud Drive / SFTP этот шаг не нужен.

Важное:
  • Выходить из игры — через игровое меню "Выйти в Windows". Это штатный
    путь, настройки и сохранения остаются на месте.
  • Cmd+Q использовать НЕ нужно — он на текущем билде может завесить
    игру (известный баг движка). Если случайно нажал и экран замер —
    Cmd+Opt+Esc → Force Quit вытащит. Это работает потому что игра
    запускается в окне (а не в эксклюзивный fullscreen).
  • Игра запускается в окне. Если нужен fullscreen — это можно настроить
    в опциях, но при следующем запуске windowed-режим включится опять
    (страховка от зависаний). Чтобы отключить эту страховку, запускайте
    через Терминал: OPENXRAY_ALLOW_FULLSCREEN=1 open /Applications/OpenXRay.app

Где что хранится:
  • Сама игра          : /Applications/STALKER-CoP/
  • Сейвы и настройки  : ~/Library/Application Support/OpenXRay/
  • Логи запуска       : ~/Library/Logs/OpenXRay/openxray.log
                         ~/Library/Application Support/OpenXRay/logs/

Если что-то не работает:
  1. Откройте /Applications/Утилиты/Терминал.app
  2. Запустите:
       tail -100 ~/Library/Logs/OpenXRay/openxray.log
     Содержимое — то, что прислать. Внутри будут пути, по которым искалась
     игра, ошибки запуска, и (если игра успела стартовать) причина краша.
  3. Если игра запустилась, но падает потом — посмотрите свежий файл в
     ~/Library/Application Support/OpenXRay/logs/  (это лог самого движка,
     там обычно конкретнее).

Требования:
  • macOS 11 (Big Sur) или новее
  • Apple Silicon (M1/M2/M3/M4). Этот DMG собран только под arm64. Для
    Intel-Mac'ов нужна пересборка с -DCMAKE_OSX_ARCHITECTURES=x86_64.
EOF

# --- 5. Build the DMG -------------------------------------------------------

echo "==> Building DMG at ${DMG_PATH}"
rm -f "${DMG_PATH}"
# UDZO = zlib-compressed read-only image. Reasonable speed, decent size on
# already-compressed game assets (most win is on text/configs).
hdiutil create \
    -volname "${VOL_NAME}" \
    -srcfolder "${STAGE_DIR}" \
    -ov \
    -format UDZO \
    "${DMG_PATH}" >/dev/null

DMG_SIZE=$(du -h "${DMG_PATH}" | awk '{print $1}')
STAGE_SIZE=$(du -sh "${STAGE_DIR}" | awk '{print $1}')

echo
echo "==> Done"
echo "  Staging dir : ${STAGE_DIR}  (${STAGE_SIZE} on disk)"
echo "  DMG         : ${DMG_PATH}  (${DMG_SIZE})"
echo
echo "Smoke test:"
echo "  hdiutil attach '${DMG_PATH}'"
echo "  open '/Volumes/${VOL_NAME}/OpenXRay.app'"
echo "  hdiutil detach '/Volumes/${VOL_NAME}'"
