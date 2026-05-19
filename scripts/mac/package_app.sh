#!/usr/bin/env bash
# Package a built xr_3da + dylibs into a macOS .app bundle.
#
# Reads inputs from environment (set defaults if missing):
#   BUILD_TYPE     CMake config used for the build (default: ReleaseMasterGold)
#   HOST_ARCH      arm64 or x86_64 (default: $(uname -m))
#   APP_VERSION    version string in Info.plist (default: dev)
#   BUNDLE_ID      reverse-DNS bundle identifier
#   PRODUCT_NAME   user-visible app name and bundle stem
#   DEFAULT_FSGAME_LTX  game data path baked into the launcher
#
# Output: dist/${PRODUCT_NAME}.app
#
# The launcher script inside Contents/MacOS execs xr_3da with -fsltx pointing
# to DEFAULT_FSGAME_LTX (overridable at runtime via the OPENXRAY_FSGAME_LTX
# env var). All Homebrew dylib references are rewritten to @rpath/<basename>
# and the bundle is ad-hoc codesigned.

set -euo pipefail

BUILD_TYPE="${BUILD_TYPE:-ReleaseMasterGold}"
HOST_ARCH="${HOST_ARCH:-$(uname -m)}"
APP_VERSION="${APP_VERSION:-dev}"
BUNDLE_ID="${BUNDLE_ID:-com.blackden.openxray}"
PRODUCT_NAME="${PRODUCT_NAME:-OpenXRay}"
DEFAULT_FSGAME_LTX="${DEFAULT_FSGAME_LTX:-\$HOME/Games/STALKER-CoP/fsgame.ltx}"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC_DIR="${REPO_ROOT}/bin/${HOST_ARCH}/${BUILD_TYPE}"
DIST_DIR="${REPO_ROOT}/dist"
APP_DIR="${DIST_DIR}/${PRODUCT_NAME}.app"
MACOS_DIR="${APP_DIR}/Contents/MacOS"

if [ ! -f "${SRC_DIR}/xr_3da" ]; then
    echo "ERROR: ${SRC_DIR}/xr_3da not found." >&2
    echo "Build the project first:  make build BUILD_TYPE=${BUILD_TYPE}" >&2
    exit 1
fi

echo "==> Cleaning ${APP_DIR}"
rm -rf "${APP_DIR}"
mkdir -p "${MACOS_DIR}" "${APP_DIR}/Contents/Resources"

echo "==> Copying engine binary and project dylibs from ${SRC_DIR}"
cp "${SRC_DIR}/xr_3da" "${MACOS_DIR}/xr_3da"
# Only ship .dylib files (engine modules). .a files are static archives, no
# need at runtime.
shopt -s nullglob
for f in "${SRC_DIR}"/*.dylib; do
    cp "$f" "${MACOS_DIR}/"
done
shopt -u nullglob

echo "==> Walking Homebrew dependency graph"
# Discover every Homebrew dylib reachable from xr_3da, follow it into the
# bundle, then keep walking until closed. We track which basenames are already
# copied via the bundle directory itself (no associative arrays needed —
# macOS ships bash 3.2 which doesn't support them).
walk_deps() {
    local binary="$1"
    otool -L "$binary" | awk 'NR>1 && $1 ~ /^\/opt\/homebrew/ {print $1}'
}
work_remaining=1
while [ "$work_remaining" = "1" ]; do
    work_remaining=0
    for binary in "${MACOS_DIR}/xr_3da" "${MACOS_DIR}"/*.dylib; do
        [ -f "$binary" ] || continue
        marker="${MACOS_DIR}/.walked-$(basename "$binary")"
        [ -f "$marker" ] && continue
        touch "$marker"
        for dep in $(walk_deps "$binary"); do
            base=$(basename "$dep")
            if [ ! -f "${MACOS_DIR}/${base}" ]; then
                [ -f "$dep" ] || { echo "ERROR: $dep not found" >&2; exit 1; }
                cp -L "$dep" "${MACOS_DIR}/${base}"
                chmod +w "${MACOS_DIR}/${base}"
                work_remaining=1
            fi
        done
    done
done
rm -f "${MACOS_DIR}"/.walked-*

echo "==> Rewriting Homebrew paths to @rpath"
install_name_tool -add_rpath "@executable_path/." "${MACOS_DIR}/xr_3da"
fix_homebrew_deps() {
    local binary="$1"
    otool -L "$binary" | awk '
        NR==1 {next}
        $1 ~ /^\/opt\/homebrew/ {print $1}
        $1 ~ /^\/usr\/local\// && $1 !~ /^\/usr\/local\/(lib|include)\// {print $1}
    ' | while read -r dep; do
        local base
        base=$(basename "$dep")
        install_name_tool -change "$dep" "@rpath/${base}" "$binary"
    done
    # Self-id of a dylib should also be @rpath-relative so other bundles can
    # link against it cleanly.
    if [[ "$binary" == *.dylib ]]; then
        install_name_tool -id "@rpath/$(basename "$binary")" "$binary"
    fi
}
for binary in "${MACOS_DIR}"/*; do
    case "$binary" in
        *.dylib) fix_homebrew_deps "$binary" ;;
        */xr_3da) fix_homebrew_deps "$binary" ;;
    esac
done

echo "==> Writing Info.plist + PkgInfo"
ICON_FRAGMENT=""
ICON_SRC="${REPO_ROOT}/scripts/mac/openxray.icns"
if [ -f "${ICON_SRC}" ]; then
    cp "${ICON_SRC}" "${APP_DIR}/Contents/Resources/openxray.icns"
    ICON_FRAGMENT="    <key>CFBundleIconFile</key>
    <string>openxray</string>
"
fi
cat > "${APP_DIR}/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>${PRODUCT_NAME}</string>
${ICON_FRAGMENT}    <key>CFBundleIdentifier</key>
    <string>${BUNDLE_ID}</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>${PRODUCT_NAME}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>${APP_VERSION}</string>
    <key>CFBundleVersion</key>
    <string>${APP_VERSION}</string>
    <key>LSMinimumSystemVersion</key>
    <string>11.0</string>
    <key>NSPrincipalClass</key>
    <string>NSApplication</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>LSApplicationCategoryType</key>
    <string>public.app-category.games</string>
</dict>
</plist>
EOF
printf "APPLOPXR" > "${APP_DIR}/Contents/PkgInfo"

echo "==> Writing launcher shim"
cat > "${MACOS_DIR}/${PRODUCT_NAME}" <<EOF
#!/bin/bash
# OpenXRay macOS launcher.
#
# Looks for fsgame.ltx in this order:
#   1. \$OPENXRAY_FSGAME_LTX (env override)
#   2. STALKER-CoP/ side-by-side with the .app (DMG / all-in-one layout)
#   3. /Applications/STALKER-CoP/fsgame.ltx
#   4. \$HOME/Applications/STALKER-CoP/fsgame.ltx
#   5. Path baked at build time: ${DEFAULT_FSGAME_LTX}
#
# Saves, configs, and engine logs go to ~/Library/Application Support/OpenXRay/
# via the engine's -overlaypath flag. Since -overlaypath is parsed with
# sscanf("%[^ ] ", ...) it rejects paths with spaces, so we route through a
# space-free symlink at ~/.openxray-data.
#
# Pass --debug as the first argument to run under lldb (requires Xcode Command
# Line Tools). Backtraces land in openxray-debug.log next to the regular log.
set -u
SCRIPT_DIR="\$(cd "\$(dirname "\${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="\${HOME}/Library/Logs/OpenXRay"
mkdir -p "\${LOG_DIR}"

# 1. Cascade-locate fsgame.ltx
FSGAME_LTX=""
for cand in \\
    "\${OPENXRAY_FSGAME_LTX:-}" \\
    "\${SCRIPT_DIR}/../../../STALKER-CoP/fsgame.ltx" \\
    "/Applications/STALKER-CoP/fsgame.ltx" \\
    "\${HOME}/Applications/STALKER-CoP/fsgame.ltx" \\
    "${DEFAULT_FSGAME_LTX}"; do
    if [ -n "\$cand" ] && [ -f "\$cand" ]; then
        FSGAME_LTX="\$cand"
        break
    fi
done
if [ -z "\$FSGAME_LTX" ]; then
    msg="OpenXRay: не найден fsgame.ltx. Положите STALKER-CoP/ рядом с OpenXRay.app (например в /Applications/), или задайте OPENXRAY_FSGAME_LTX."
    echo "\$msg" >> "\${LOG_DIR}/openxray.log"
    osascript -e "display alert \\"OpenXRay\\" message \\"\$msg\\"" >/dev/null 2>&1 || true
    exit 1
fi

# 2. Set up writable _appdata_ location outside the bundle
APPDATA_DIR="\${HOME}/Library/Application Support/OpenXRay"
mkdir -p "\${APPDATA_DIR}/_appdata_"
OVERLAY_LINK="\${HOME}/.openxray-data"
if [ ! -L "\${OVERLAY_LINK}" ] || [ "\$(readlink "\${OVERLAY_LINK}")" != "\${APPDATA_DIR}" ]; then
    rm -f "\${OVERLAY_LINK}"
    ln -s "\${APPDATA_DIR}" "\${OVERLAY_LINK}"
fi

# 2a. Force windowed mode in user.ltx (stability escape hatch).
# Exclusive fullscreen makes the GPU driver hard to recover from on macOS:
# if the engine hangs, Cmd+Tab / Force Quit dialog / Activity Monitor are
# the only ways to recover, and they all need the window to be a normal
# NSWindow (not a fullscreen layer). Set OPENXRAY_ALLOW_FULLSCREEN=1 to
# opt out and let user.ltx control the window mode normally.
USER_LTX="\${APPDATA_DIR}/_appdata_/user.ltx"
touch "\$USER_LTX"
upsert_ltx() {
    local kv="\$1"; local key="\${kv%% *}"
    if grep -qE "^\$key " "\$USER_LTX"; then
        sed -i '' -e "s|^\$key .*|\$kv|" "\$USER_LTX"
    else
        printf '%s\\n' "\$kv" >> "\$USER_LTX"
    fi
}
if [ "\${OPENXRAY_ALLOW_FULLSCREEN:-0}" != "1" ]; then
    upsert_ltx "vid_window_mode st_opt_windowed"
    upsert_ltx "rs_fullscreen off"
fi

# 2b. Safe-mode boot recovery.
# A sentinel file is created right before exec and removed by the engine
# after stable boot (CDevice::Initialize completes successfully, see
# IGame_Persistent's "boot stable" hook). If we see the sentinel from a
# previous launch, that means the previous run crashed / hung / was
# force-killed before reaching the stable point. Drop graphics to the
# safest minimum so the player can get back into the menu and adjust.
# Sentinel lives inside _appdata_/, mirroring what the engine resolves
# for \$app_data_root\$ even under -overlaypath (the fsgame.ltx rule
# appends the _appdata_\ segment regardless of which root is set).
SAFE_SENTINEL="\${APPDATA_DIR}/_appdata_/.boot_in_progress"
if [ -f "\$SAFE_SENTINEL" ] && [ "\${OPENXRAY_FORCE_NORMAL_BOOT:-0}" != "1" ]; then
    echo "==> SAFE MODE: previous launch did not reach stable boot. Resetting graphics to minimal." >> "\${LOG_DIR}/openxray.log"
    upsert_ltx "vid_mode 1280x720"
    upsert_ltx "vid_window_mode st_opt_windowed"
    upsert_ltx "rs_fullscreen off"
    upsert_ltx "r__supersample 0"
    upsert_ltx "r2_aa off"
    upsert_ltx "r2_sun off"
    upsert_ltx "r2_sun_quality 0"
    upsert_ltx "r2_shadow_cascade_old on"
    upsert_ltx "r__detail_density 0.6"
    upsert_ltx "r__geometry_lod 0.6"
    upsert_ltx "rs_vis_distance 0.4"
    upsert_ltx "r2_dof off"
    upsert_ltx "r2_ls_bloom_fast on"
fi
# Drop sentinel so this launch is now "in progress". Engine deletes it
# after stable boot; if it lingers, next launch hits safe mode.
touch "\$SAFE_SENTINEL"

# 3. Launch
if [ "\${1:-}" = "--debug" ]; then
    shift
    if command -v lldb >/dev/null 2>&1; then
        exec lldb --batch \\
            -o "process launch -- -fsltx \${FSGAME_LTX} -overlaypath \${OVERLAY_LINK}/ -nointro \$*" \\
            -k "thread backtrace all" \\
            -k "quit" \\
            "\${SCRIPT_DIR}/xr_3da" \\
            >> "\${LOG_DIR}/openxray-debug.log" 2>&1
    else
        echo "WARNING: --debug requested but lldb not found on PATH." \\
            "Install Xcode Command Line Tools (xcode-select --install) to enable." \\
            >> "\${LOG_DIR}/openxray-debug.log"
        # Fall through to normal launch
    fi
fi
exec "\${SCRIPT_DIR}/xr_3da" \\
    -fsltx "\${FSGAME_LTX}" \\
    -overlaypath "\${OVERLAY_LINK}/" \\
    -nointro "\$@" \\
    >> "\${LOG_DIR}/openxray.log" 2>&1
EOF
chmod +x "${MACOS_DIR}/${PRODUCT_NAME}"

echo "==> Ad-hoc codesigning bundle"
# Two-pass sign: first the bundle deep (signs all dylibs, launcher), then
# re-sign xr_3da with the get-task-allow entitlement so lldb can attach on
# the user's machine (without that entitlement macOS refuses attach even to
# an ad-hoc binary the user owns). The entitlement only applies to xr_3da;
# the rest of the bundle stays under plain ad-hoc sign. This is fine for
# personal builds, not for App Store distribution.
ENTITLEMENTS="${REPO_ROOT}/scripts/mac/debug.entitlements"
codesign --force --deep --sign - "${APP_DIR}" >/dev/null
codesign --force --sign - --entitlements "${ENTITLEMENTS}" \
    "${MACOS_DIR}/xr_3da" >/dev/null

echo
echo "==> Done"
echo "  Bundle: ${APP_DIR}"
echo "  Launch: open '${APP_DIR}'"
echo "  Game data path baked into launcher: ${DEFAULT_FSGAME_LTX}"
echo "  Override at runtime: OPENXRAY_FSGAME_LTX=/path/to/fsgame.ltx open '${APP_DIR}'"
