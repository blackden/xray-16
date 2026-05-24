# macOS — distribution cheat sheet

How we ship OpenXRay builds to other Macs, and the CLI bits worth knowing.

## Quick reference

```bash
# Build artefacts (run from a worktree with gamedata at $GAME_DIR)
make package          # → dist/OpenXRay.app                (~80 MB, binary + launcher)
make all-in-one       # → dist/OpenXRay-AllInOne.dmg       (~3.5 GB, .app + gamedata)

# Repackage an .app as a codesign-preserving zip for messengers
ditto -c -k --sequesterRsrc --keepParent dist/OpenXRay.app dist/OpenXRay.app.zip
```

## DMG (disk image) lifecycle

DMG = filesystem in a single file. Mount it with `hdiutil` and macOS exposes
the contents under `/Volumes/<VolumeLabel>/`. Unmount before deleting the
DMG or ejecting from Finder — otherwise the mount lingers and burns RAM.

### Mount

```bash
hdiutil attach dist/OpenXRay-AllInOne.dmg
# Mounted at /Volumes/OpenXRay-AllInOne/
ls "/Volumes/OpenXRay-AllInOne/"
# OpenXRay.app  STALKER-CoP  README.txt
```

Options worth knowing:

- `-readonly` — refuse writes (useful when you only need to inspect).
- `-mountpoint <dir>` — pin the mount to a chosen directory instead of `/Volumes/<VolumeLabel>/`.
- `-nobrowse` — keep the volume out of Finder's sidebar (handy for scripted attach/detach).

### Unmount

```bash
hdiutil detach "/Volumes/OpenXRay-AllInOne"
# Force-detach if the volume is "in use" by some lingering process:
hdiutil detach -force "/Volumes/OpenXRay-AllInOne"
```

`hdiutil eject` is an alias for `detach`; either works.

### List mounted images

```bash
hdiutil info
# Look for "image-path : ..." entries and the "/Volumes/..." mountpoints
# they expose.
```

### Inspect a DMG without mounting

```bash
hdiutil imageinfo dist/OpenXRay-AllInOne.dmg
# Prints the partition layout, checksums, encryption status, compression
# type (UDZO for ours), etc.
```

### Make a DMG by hand (what `make all-in-one` does under the hood)

```bash
hdiutil create \
    -volname "OpenXRay-AllInOne" \
    -srcfolder dist/all-in-one \
    -ov -format UDZO \
    -fs HFS+ \
    dist/OpenXRay-AllInOne.dmg
```

- `-srcfolder <dir>` — point at the staging dir whose contents become the
  filesystem root of the DMG.
- `-format UDZO` — bzip2-style compressed read-only. Best size/speed for
  distribution.
- `-fs HFS+` — old but universal. Apple-only readers do fine; we don't
  need exFAT compatibility here.
- **No spaces in `-volname`.** The engine parses `-fsltx` / `-overlaypath`
  with `sscanf("%[^ ] ", ...)`, which truncates the value at the first
  whitespace — so a mount under `/Volumes/OpenXRay All-in-One/` would
  crash the launcher with `Cannot open file "/Volumes/OpenXRay"`.

## Gatekeeper / quarantine workaround

macOS marks anything downloaded from a browser or messenger with the
`com.apple.quarantine` extended attribute. Gatekeeper sees that flag and
refuses to launch ad-hoc-signed (i.e. unnotarized) bundles. Symptoms:
"is damaged" alert, or a silent refusal on double-click.

```bash
# Strip the quarantine attr from one bundle:
xattr -dr com.apple.quarantine /Applications/OpenXRay.app

# Verify before / after:
xattr -l /Applications/OpenXRay.app
```

After that, double-click works. On stricter macOS versions you may still
get one prompt: System Settings → Privacy & Security → look for the
"xr_3da was blocked" line → "Open Anyway". Approved once, remembered.

## Notarization (for actual public release)

Ad-hoc signed bundles always need the quarantine workaround. To make a
build that any Mac opens silently:

1. Enroll in Apple Developer Program ($99/yr).
2. Sign the bundle with a "Developer ID Application" certificate via
   `codesign --options runtime --timestamp -s "Developer ID Application: ..." ...`.
3. Submit to Apple's notary service: `xcrun notarytool submit dist/OpenXRay.app.zip --keychain-profile ...`.
4. Staple the ticket: `xcrun stapler staple dist/OpenXRay.app`.

Then any DMG containing the stapled .app opens with no quarantine prompt.

## Codesign-preserving zip (so the recipient doesn't see "damaged")

`zip -r` mangles xattrs and resource forks on macOS. Use `ditto`:

```bash
# Compress (preserves codesign, xattrs, resource forks)
ditto -c -k --sequesterRsrc --keepParent OpenXRay.app OpenXRay.app.zip

# Decompress (mirror flag set)
ditto -x -k OpenXRay.app.zip /destination/
```

`ditto` is Apple's preferred tool — it's how Xcode's `Archive → Export`
packages signed apps for distribution.

## Bundled tools (xrUnpack)

`make package` / `make all-in-one` теперь кладёт `xrUnpack` рядом с
`xr_3da` в `Contents/MacOS/`. Это значит, что любой получатель DMG
может распаковать vanilla `.db?` без отдельной установки dev-tree:

```bash
/Applications/OpenXRay.app/Contents/MacOS/xrUnpack \
    ~/Games/STALKER-CoP/resources/configs.db \
    ~/Desktop/cop-configs
```

Полный workflow с modding'ом описан в `notes/cheatsheet.md` (секция
"Modding workflow"). Внутри .app пользователь работает с одним
сторонним инструментом; никакого CMake/Homebrew не нужно.

## Install instructions (paste these to a friend)

```bash
# 1) Unpack
cd ~/Downloads
ditto -x -k OpenXRay.app.zip .

# 2) Move into Applications (keep your existing STALKER-CoP/ folder intact)
rm -rf /Applications/OpenXRay.app
mv OpenXRay.app /Applications/

# 3) Strip the quarantine that Telegram/Safari attached
xattr -dr com.apple.quarantine /Applications/OpenXRay.app

# 4) Launch
open /Applications/OpenXRay.app
```

If macOS still complains:
- System Settings → Privacy & Security → bottom: "xr_3da was blocked" →
  "Open Anyway". Once. Remembered after.

## fsgame.ltx cascade (where the launcher looks)

The launcher in `Contents/MacOS/OpenXRay` (shell script) searches in this
order:

1. `$OPENXRAY_FSGAME_LTX` (env var override)
2. `STALKER-CoP/fsgame.ltx` side-by-side with the .app
3. `/Applications/STALKER-CoP/fsgame.ltx`
4. `~/Applications/STALKER-CoP/fsgame.ltx`
5. The default baked at build time (`scripts/mac/package_app.sh`,
   `$DEFAULT_FSGAME_LTX`, typically `~/Games/STALKER-CoP/fsgame.ltx`)

Any of those paths must be space-free (engine `-fsltx` parser truncates
at whitespace). If yours has spaces, route via a symlink:

```bash
ln -s "/Volumes/My Big Drive/Games/STALKER-CoP" "$HOME/STALKER-CoP"
OPENXRAY_FSGAME_LTX="$HOME/STALKER-CoP/fsgame.ltx" open /Applications/OpenXRay.app
```
