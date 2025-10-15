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

Artifacts appear under `build/macos-arm64/<Config>/`. Presets use Ninja and honor `CMAKE_BUILD_PARALLEL_LEVEL`, so the bootstrap default (`sysctl -n hw.ncpu`) gives the same effect as `make -j$(sysctl -n hw.ncpu)`. You can also call them manually with `cmake --preset macos-arm64-relwithdebinfo` and `cmake --build --preset macos-arm64-relwithdebinfo-build`.

## Game data & Run
1. Keep the repository `res/` directory in place. If you want resources alongside the build output, create a symlink instead of copying:
   ```bash
   CONFIG=RelWithDebInfo
   BUILD_DIR="${PWD}/build/macos-arm64/${CONFIG}"
   ln -sfn "${PWD}/res" "${BUILD_DIR}/res"
   ```
2. Place your S.T.A.L.K.E.R. Windows game assets (e.g. `gamedata`, `.db*`, `levels`, `localization`, `resources`) next to `res/fsgame.ltx`. Copy or symlink them from the Windows installation. Shaders shipped in this repository already live under `res/gamedata/shaders/`.
3. Launch the engine (from any directory):
   ```bash
   "${BUILD_DIR}/bin/xr_3da" -fsltx "${PWD}/res/fsgame.ltx" -renderer gl4
   ```
   `-renderer gl4` is the supported option on macOS; other renderers usually fail with “Can't setup renderer”.

## Logs & diagnostics
- Every helper script writes a timestamped log under `logs/` (e.g. `logs/build_YYYYMMDD_HHMMSS.log`). The latest build log is also symlinked to `logs/latest_build.log`.
- Use `NO_LOG=1` to disable log capture temporarily.
- After configure, copies of `CMakeOutput.log` / `CMakeError.log` (if produced) are stored alongside the build log for quick sharing.
- `ci/local-build.sh` chains bootstrap + build and keeps its own log.
- Optional runtime helper: `scripts/run-with-log.sh --config RelWithDebInfo -- -fsltx ./res/fsgame.ltx -renderer gl4` launches the engine with `-log` and copies generated `xray_*.log` files into `logs/run_<timestamp>/`.

## Troubleshooting
- Collect a backtrace with LLDB:
  ```bash
  lldb -- "${PWD}/build/macos-arm64/RelWithDebInfo/bin/xr_3da" --args -fsltx "${PWD}/res/fsgame.ltx" -renderer gl4
  ```
- Ensure `brew bundle` completed successfully; missing libraries cause CMake configuration errors.
- Delete `build/macos-arm64/` if you need a clean reconfigure.
- “Can't setup renderer” typically means the data path is incorrect. Verify `-fsltx` points to the repository `res/fsgame.ltx` and the required game archives are present.

## Architecture policy
ARM64-only. x86_64, Rosetta, and universal binaries are out of scope for this workflow.
