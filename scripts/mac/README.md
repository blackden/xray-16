# macOS helper scripts

## Installing S.T.A.L.K.E.R. (Call of Pripyat) via steamcmd

OpenXRay is just the engine. To run it, you need game data (`gamedata` /
`.db*`-archives and `fsgame.ltx`) from an owned copy of the game. There's
no native Steam build of CoP/CS on macOS, so we install the Windows depot
through `steamcmd`.

### Prereqs

- A Steam account that owns the title:
  - Call of Pripyat — appid `41700`
  - Clear Sky — appid `20510`
- `steamcmd` installed: `brew install --cask steamcmd`.

### Install

Preferred — via the top-level Makefile:

```bash
make install-game STEAM_LOGIN=yourname
```

Override target / install dir:

```bash
make install-game STEAM_LOGIN=yourname APPID=20510 GAME_DIR=$HOME/Games/STALKER-CS
```

Or call the script directly (same env-var interface):

```bash
STEAM_LOGIN=yourname scripts/mac/install-cop-steamcmd.sh
STEAM_LOGIN=yourname INSTALL_DIR=$HOME/Games/STALKER-CS APPID=20510 \
  scripts/mac/install-cop-steamcmd.sh
```

On first login Steam Guard sends a code by email; `steamcmd` will prompt
for it. Credentials are cached, later runs are non-interactive.

### After install

If you used the default `GAME_DIR` (`$HOME/Games/STALKER-CoP`), `make run`
already points `FSGAME_LTX` there — just:

```bash
make run
```

If you installed elsewhere, pass it explicitly:

```bash
make run FSGAME_LTX="$HOME/Games/STALKER-CS/fsgame.ltx"
```

### Notes

- `+@sSteamCmdForcePlatformType windows` is mandatory and must come before
  `+login` — otherwise Steam refuses to serve a non-existent macOS depot.
- Call of Chernobyl is a mod, not a Steam title. Install on top of a
  working CoP separately.
- The script is idempotent (`app_update ... validate`) — re-running it
  resumes or repairs a broken install.
