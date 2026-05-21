#!/usr/bin/env bash
# Install S.T.A.L.K.E.R.: Call of Pripyat (or Clear Sky) on macOS via steamcmd.
# Uses Steam's Windows depot — neither game ships a native macOS build.
set -euo pipefail

STEAM_LOGIN="${STEAM_LOGIN:-${1:-}}"
INSTALL_DIR="${INSTALL_DIR:-${2:-$HOME/Games/STALKER-CoP}}"
APPID="${APPID:-41700}"  # 41700 = Call of Pripyat, 20510 = Clear Sky
LANGUAGE="${LANGUAGE:-${3:-}}"  # optional: english, russian, french, german, italian, spanish

usage() {
  cat <<USAGE >&2
Usage: STEAM_LOGIN=<login> [INSTALL_DIR=<path>] [APPID=<id>] [LANGUAGE=<lang>] $0
   or: $0 <login> [<install_dir>] [<language>]

Environment variables:
  STEAM_LOGIN  Your Steam account name (you must own the game).
  INSTALL_DIR  Where to install (default: \$HOME/Games/STALKER-CoP).
  APPID        Steam app id: 41700 = Call of Pripyat (default),
                             20510 = Clear Sky.
  LANGUAGE     Optional Steam language depot to download alongside the
               default English. Valid values per CoP install.vdf:
                 english, russian, french, german, italian, spanish.
               If unset, only the default (english) voice/text is fetched.

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

LANG_ARGS=()
if [[ -n "$LANGUAGE" ]]; then
  LANG_ARGS=(-language "$LANGUAGE")
  echo "==> Installing appid $APPID (language: $LANGUAGE) into $INSTALL_DIR as Steam user '$STEAM_LOGIN'"
else
  echo "==> Installing appid $APPID (default language) into $INSTALL_DIR as Steam user '$STEAM_LOGIN'"
fi

steamcmd \
  +@sSteamCmdForcePlatformType windows \
  +force_install_dir "$INSTALL_DIR" \
  +login "$STEAM_LOGIN" \
  +app_update "$APPID" "${LANG_ARGS[@]}" validate \
  +quit

FSGAME_LTX="$INSTALL_DIR/fsgame.ltx"
echo
if [[ -f "$FSGAME_LTX" ]]; then
  echo "==> Done. fsgame.ltx is at:"
  echo "    $FSGAME_LTX"
  if [[ -n "$LANGUAGE" ]]; then
    echo
    echo "==> Language pack '$LANGUAGE' was requested. Verify the DB landed:"
    echo "    ls \"$INSTALL_DIR/localization/\""
    echo "    (Expect: x${LANGUAGE}.db, e.g. xrussian.db for LANGUAGE=russian.)"
    echo
    echo "==> To switch the game to that language, in your user.ltx set:"
    echo "    g_language ${LANGUAGE:0:3}     # e.g. rus for russian, eng for english"
    echo "    Then in-game console:  snd_restart"
  fi
  echo
  echo "==> Next step:"
  echo "    make run FSGAME_LTX=\"$FSGAME_LTX\""
else
  echo "==> Install finished but fsgame.ltx was not found at:"
  echo "    $FSGAME_LTX"
  echo "    Check the install dir layout and locate it manually."
fi
