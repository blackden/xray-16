# Codex Observations

## Repository Focus
- OpenXRay is a community-driven evolution of the X-Ray engine for S.T.A.L.K.E.R., targeting fidelity with added stability and performance (README.md).
- Supported games: Call of Chernobyl 1.4.22, Call of Pripyat 1.6.02, Clear Sky 1.5.10; Shadow of Chernobyl remains out of scope for now (README.md).

## Build & Tooling
- Core build orchestrated through CMake 3.23+ with shared logic in `cmake/XRay.*` modules and top-level `CMakeLists.txt`.
- `CMakePresets.json` provides Ninja presets for macOS ARM64 (Debug, RelWithDebInfo, Release), fixing `CMAKE_OSX_ARCHITECTURES=arm64` and generating under `build/macos-arm64/<Config>/`.
- `Brewfile` lists Homebrew dependencies (cmake, ninja, SDL2, libogg/vorbis, theora, lzo, mimalloc, jpeg-turbo, openal-soft) for one-shot installation.
- `scripts/bootstrap-macos.sh` enforces macOS requirements, installs Brew packages, captures environment defaults, and writes timestamped logs.
- `scripts/build.sh` wraps preset-driven configure/build steps, copies `CMakeOutput.log` / `CMakeError.log`, and maintains `logs/latest_build.log`.

## Code Structure
- `src/` groups engine subsystems (xrCore, xrEngine, xrGame, xrAICore, xrPhysics, xrUICore, etc.) with modular `add_subdirectory` hierarchy defined in `src/CMakeLists.txt`.
- External dependencies (LuaJIT, luabind, ODE, GameSpy, imgui, tracy, etc.) reside under `Externals/`, with safeguards ensuring a git clone rather than archive download.
- Project carries Visual Studio solutions (`src/engine.sln`, property sheets) alongside cross-platform CMake files, enabling Windows builds.

## Documentation & Utilities
- macOS ARM64 build docs exist in English/Russian (`docs/build-macos.en.md`, `docs/build-macos.ru.md`), guiding two-command bootstrap/build workflow plus runtime setup.
- `scripts/run-with-log.sh` launches `xr_3da`, forces `-log`, and aggregates runtime logs into `logs/run_<timestamp>/`.
- `ci/local-build.sh` chains bootstrap + RelWithDebInfo build, mirroring an on-machine CI pass while logging to `logs/`.
- `logs/bootstrap_20251015_124115.log` illustrates successful bootstrap with auto-detected `CMAKE_BUILD_PARALLEL_LEVEL=10`.

## Process Notes
- `Бриф для codex.md` codifies macOS ARM64-only policy, expected automation artifacts, and commit ordering for incremental history.
- Log-centric scripting, environment snapshots, and preset alignment reflect emphasis on reproducibility and newcomer-friendly workflow.

## Runtime Log Snapshot (2025-10-15)
- `logs/openxray_backtrace-2025_10_15-18_49.log:10` records an LLDB launch of `/Users/ragnar/stalker_cop/bin/xr_3da` with `-fsltx ./fsgame.ltx -w -nosound -nointro -log`.
- `logs/openxray_backtrace-2025_10_15-18_49.log:28` reports only `renderer_r3` as an available render mode, matching the sparse scene (sky and weapon silhouette).
- `logs/openxray_backtrace-2025_10_15-18_49.log:60` captures GLSL compilation failures for `gl\accum_sun_near_nomsaa_nominmax.ps`, preventing proper lighting.
- `/Users/ragnar/stalker_cop/_appdata_/logs/openxray_ragnar.log:75081` lists missing UI textures and OpenGL errors for resource bindings during the same session.
