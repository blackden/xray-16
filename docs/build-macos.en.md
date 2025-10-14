# OpenXRay macOS ARM64 Build Guide

This guide targets Apple Silicon machines only. The tooling assumes ARM64 everywhere and does not produce universal binaries.

## Prerequisites
- macOS 13 Ventura or newer on Apple Silicon.
- [Xcode Command Line Tools](https://developer.apple.com/xcode/resources/): `xcode-select --install`.
- [Homebrew](https://brew.sh/) in `/opt/homebrew`.
- Git clone (with submodules):  
  ```bash
  git clone https://github.com/blackden/xray-16.git --recurse-submodules
  cd xray-16
  ```

## One-time setup
Install build dependencies and prepare defaults:

```bash
./scripts/bootstrap-macos.sh
```

The script runs `brew bundle`, verifies Xcode CLT presence, and stores a recommended `CMAKE_BUILD_PARALLEL_LEVEL` derived from your CPU core count.

## Build
Use the helper script (defaults to `RelWithDebInfo`):

```bash
./scripts/build.sh                    # RelWithDebInfo
./scripts/build.sh Debug              # Debug
./scripts/build.sh Release            # Release
```

Artifacts appear under `build/macos-arm64/<Config>/`. The presets behind the script can also be triggered manually with `cmake --preset macos-arm64-relwithdebinfo` and `cmake --build --preset macos-arm64-relwithdebinfo-build`.

## Portable layout & Run
1. Copy the engine resources to the build output:
   ```bash
   CONFIG=RelWithDebInfo
   BUILD_DIR="build/macos-arm64/${CONFIG}"
   cp -R res "${BUILD_DIR}/"
   ```
2. Place your S.T.A.L.K.E.R. game data (e.g. `gamedata`, `.db` archives) next to `res/fsgame.ltx`.
3. Launch the engine from the build directory:
   ```bash
   cd "${BUILD_DIR}/bin"
   ./xr_3da -fsltx "${BUILD_DIR}/res/fsgame.ltx" -renderer gl4
   ```

## Troubleshooting
- Collect a backtrace with LLDB:
  ```bash
  lldb -- "${PWD}/build/macos-arm64/RelWithDebInfo/bin/xr_3da" --args -fsltx "${PWD}/build/macos-arm64/RelWithDebInfo/res/fsgame.ltx" -renderer gl4
  ```
- Ensure `brew bundle` completed successfully; missing libraries cause CMake configuration errors.
- Delete `build/macos-arm64/` if you need a clean reconfigure.

## Architecture policy
ARM64-only. x86_64, Rosetta, and universal binaries are out of scope for this workflow.
