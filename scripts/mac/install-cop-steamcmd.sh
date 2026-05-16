#!/usr/bin/env bash
# Install S.T.A.L.K.E.R.: Call of Pripyat (or Clear Sky) on macOS via steamcmd.
# Uses Steam's Windows depot — neither game ships a native macOS build.
set -euo pipefail

STEAM_LOGIN="${STEAM_LOGIN:-${1:-}}"
INSTALL_DIR="${INSTALL_DIR:-${2:-$HOME/Games/STALKER-CoP}}"
APPID="${APPID:-41700}"  # 41700 = Call of Pripyat, 20510 = Clear Sky

usage() {
  cat <<USAGE >&2
Usage: STEAM_LOGIN=<login> [INSTALL_DIR=<path>] [APPID=<id>] $0
   or: $0 <login> [<install_dir>]

Environment variables:
  STEAM_LOGIN  Your Steam account name (you must own the game).
  INSTALL_DIR  Where to install (default: \$HOME/Games/STALKER-CoP).
  APPID        Steam app id: 41700 = Call of Pripyat (default),
                             20510 = Clear Sky.

Notes:
  * Steam Guard: steamcmd will prompt for the code on first login;
    subsequent runs reuse the saved credentials.
  * +@sSteamCmdForcePlatformType windows is mandatory and must come
    before +login.
  * Call of Chernobyl is a mod, not a Steam title — install it manually
    on top of a working CoP.
USAGE
}

if [[ -z "$STEAM_LOGIN" ]]; then
  usage
  exit 1
fi

if ! command -v steamcmd >/dev/null 2>&1; then
  echo "ERROR: steamcmd not found in PATH." >&2
  echo "       Install with: brew install --cask steamcmd" >&2
  exit 1
fi

mkdir -p "$INSTALL_DIR"

echo "==> Installing appid $APPID into $INSTALL_DIR as Steam user '$STEAM_LOGIN'"
steamcmd \
  +@sSteamCmdForcePlatformType windows \
  +force_install_dir "$INSTALL_DIR" \
  +login "$STEAM_LOGIN" \
  +app_update "$APPID" validate \
  +quit

FSGAME_LTX="$INSTALL_DIR/fsgame.ltx"
echo
if [[ -f "$FSGAME_LTX" ]]; then
  echo "==> Done. fsgame.ltx is at:"
  echo "    $FSGAME_LTX"
  echo
  echo "==> Next step:"
  echo "    make run FSGAME_LTX=\"$FSGAME_LTX\""
else
  echo "==> Install finished but fsgame.ltx was not found at:"
  echo "    $FSGAME_LTX"
  echo "    Check the install dir layout and locate it manually."
fi
