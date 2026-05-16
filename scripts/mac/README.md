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

```bash
STEAM_LOGIN=yourname scripts/mac/install-cop-steamcmd.sh
```

Overrides:

```bash
STEAM_LOGIN=yourname \
INSTALL_DIR=$HOME/Games/STALKER-CS \
APPID=20510 \
  scripts/mac/install-cop-steamcmd.sh
```

On first login Steam Guard sends a code by email; `steamcmd` will prompt
for it. Credentials are cached, later runs are non-interactive.

### After install

The script prints the resolved `fsgame.ltx` path. Plug it into the
top-level `Makefile`:

```bash
make run FSGAME_LTX="$HOME/Games/STALKER-CoP/fsgame.ltx"
```

### Notes

- `+@sSteamCmdForcePlatformType windows` is mandatory and must come before
  `+login` — otherwise Steam refuses to serve a non-existent macOS depot.
- Call of Chernobyl is a mod, not a Steam title. Install on top of a
  working CoP separately.
- The script is idempotent (`app_update ... validate`) — re-running it
  resumes or repairs a broken install.
