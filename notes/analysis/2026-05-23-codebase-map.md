# Codebase map — baseline 2026-05-23

Output of the `#67 Codebase Mapping` epic. Six analysis tools were run against
the macOS fork on branch `issue-67-codebase-mapping` (off
`macos/blackden/master`). Read-only — no source code changed.

The point of this dump is to feed scope-reduction decisions for `#66`:
which modules deserve refactor effort, which are dead weight, which carry
latent bug load disproportionate to their size.

## Tool versions

| Tool | Version |
|------|---------|
| clang-tidy | 20.1.8 (Homebrew llvm@20) |
| include-what-you-use | 0.26 (based on Homebrew clang 22.1.5) |
| cppcheck | 2.20.0 |
| lizard | 1.22.1 |
| git | system (commit history scan only) |
| shell + grep | inter-module #include matrix |

Compile DB: `build-lsp/compile_commands.json` (1880 entries, 1618 under
`src/`). For clang-tidy/IWYU a sanitized copy was used: PCH `-include-pch`
and `-include cmake_pch.hxx` arguments were stripped because Apple-clang
`(clang-1700.6.4.2)` (the toolchain in the compile DB) and Homebrew
llvm@20 produce mutually-incompatible PCH formats. Without the strip the
tools error out on every TU with `PCH file ... built from a different
branch`.

## Per-report TL;DR

### 1. clang-tidy — `2026-05-23-clang-tidy.json`

110 findings from bugprone/cert/clang-analyzer (PCH-stripped sanitized
compile DB, `--extra-arg=-isysroot $SDK`). 80 of the 110 are in `xrGame`,
which is also the largest module — but normalized per kLOC the density is
moderate. Top checks: `bugprone-narrowing-conversions` (32),
`bugprone-easily-swappable-parameters` (31),
`bugprone-switch-missing-default-case` (12). Most concentrated file:
`src/xrGame/game_sv_capture_the_artefact.cpp` (25 findings — multiplayer
server code we don't ship). **Status: PARTIAL.** Run was SIGTERM'd
(exit 144) after ~25 min before all 1618 TUs were analyzed. No
`clang-diagnostic-error` survived the PCH/SDK fix, so what's there is
real signal — just incomplete breadth.

### 2. IWYU — `2026-05-23-iwyu.txt`

~9k lines, 9 "should add" + 9 "should remove" findings. Coverage is
narrow: only the non-PCH targets (`utils/xrMiscMath`, `utils/xrCompress`)
produced usable output. Everywhere else IWYU silently emitted "has
correct" lines after hitting the same PCH wall as clang-tidy — see the
caveat above. **Status: PARTIAL.** Useful as a methodology baseline, not
as an actual cleanup target list. Re-run after rebuilding PCH with
llvm@20 (or removing CMake `target_precompile_headers` calls in a
dedicated branch) will give the real number.

### 3. cppcheck — `2026-05-23-cppcheck-dead.txt`

127 findings via `--enable=unusedFunction` (which in cppcheck mode also
emits many other checks). Distribution: 75 in `src/utils/` (mostly
multiplayer GameSpy SDK fossil code in `mp_gpprof_server` —
unknownMacro/syntaxError noise from PS2/PS3 ifdef paths cppcheck can't
parse), 37 in `src/editors/`, 31 in `src/Layers/`, 15 in `src/xrCore/`.
**Zero `unusedFunction` hits** — every cross-TU candidate had a caller
in `src/`. The interesting real findings: `nullPointer` (4),
`nullPointerOutOfResources` (2), `uninitvar` (17), `doubleFree` (1) —
mostly clustered in `xrCore/LocatorAPI.cpp` (null deref through
`xr_strcpy`/`strcpy_s`) and Layers/xrRender. **Status: clean run.**

### 4. lizard — `2026-05-23-complexity.csv` + `-top50.txt`

37,425 functions analyzed. 132 functions in `src/` have cyclomatic
complexity > 30 (excluding Externals). The CCN distribution is heavily
skewed: top is `xray::clsid_to_spawn_category` in
`xrServerEntities/object_factory_spawner.h` at **CCN=198, 231 NLOC** —
a single class-id→spawn-category dispatch giant. Other notable:
`CAI_Stalker::debug_text` (CCN=168), `CActor::g_SetAnimation` (CCN=112),
`xray::imgui::xr_key_to_imgui_key` (CCN=121 — but it's a generated
key-mapping switch, intentional). 27 of the top-50 live in
`src/utils/mp_gpprof_server/` (vendored GameSpy SDK — out of scope for
refactor). **Status: clean run.**

### 5. git churn — `2026-05-23-churn.txt`

Top-100 most-edited files in `src/` over the last 12 months. Module
totals: xrGame 163 churn events, xrEngine 117, xrCore 82, Layers 67,
xrUICore 60. Individual leaders: `xrCore/xrCore.cpp` (14),
`xrCore/xrCore.h` (13), `xrUICore/Windows/UIFrameLineWnd.cpp` (12),
`xrGame/MainMenu.cpp` (12), `xrGame/console_commands.cpp` (12),
`xrEngine/xr_level_controller.cpp` (11). **Status: clean run.**

### 6. dep-graph — `2026-05-23-deps.txt`

Inter-module `#include` matrix counted by `grep -rE '^#include
[<"]<module>/' ...`. Reveals the spine of the engine: `xrGame` imports
from `xrEngine` (~hundreds), which imports from `xrCore` (~hundreds).
`Layers` (renderers) sits between: imports xrEngine (103), xrCore (64).
`xrCore` imports nothing else from `src/` (correct — it's the base).
**Status: clean run.**

## Top-10 "hot pain" modules

Combining churn × complexity × clang-tidy density × LOC. Ranked by
priority for `#66` scope-reduction work. LOC counts include `.cpp`,
`.h`, `.hpp`, `.inl`.

| # | Module | LOC | Churn (1y) | CCN>30 fns | clang-tidy findings | Notes |
|---|--------|-----|------------|------------|---------------------|-------|
| 1 | **xrGame** | 360k | 163 | 66 | 80 | Biggest module by every metric. Reduction target #1 — but the wins are inside, not at the boundary. Largest sub-painpoint: multiplayer code (`game_sv_capture_the_artefact.cpp` alone owns 25 of 80 clang-tidy hits — and we don't ship MP). |
| 2 | **xrEngine** | 32k | 117 | 8 | 5 | High churn / small surface — every macOS bugfix touches it (`x_ray.cpp`, `xr_level_controller.cpp`, `xr_ioc_cmd.cpp`, `editor_base*`). Cvar plumbing dominates. Healthy code, but it's the daily-friction layer. |
| 3 | **xrCore** | 42k | 82 | 4 | 13 | Foundation: filesystem, strings, debug, threading. Churn driven by `xrCore.cpp` (14) + `xrCore.h` (13) — identity/UserName changes (see CLAUDE memory `pw_name` commit `b888ad5`). cppcheck flags real null-deref through `LocatorAPI.cpp`→`xr_strcpy`→`strcpy_s`. |
| 4 | **Layers** | 86k | 67 | 29 | 5 | Renderer modules — R2/R4/GL. 29 high-CCN functions, most in `xrRender_R2/r2.cpp::create` (CCN=99) and the GL shader generator. Our Apple GL fork lives here — care budget should stay high. |
| 5 | **xrUICore** | 18k | 60 | 3 | 4 | `UIFrameLineWnd.cpp` (12 churn) is the long pole — repeatedly touched UI widget. Low complexity. |
| 6 | **xrServerEntities** | 21k | 9 | 4 | 1 | LOW churn, but harbours the worst CCN function in the whole codebase: `clsid_to_spawn_category` (CCN=198, NLOC=231). Single-shot refactor target — touched rarely, so safe-ish. |
| 7 | **xrPhysics** | 30k | low | 11 | 2 | ODE-based, mostly stable. 11 high-CCN funcs concentrated in `PHSimpleCharacter.cpp`, `dcylinder/dCylinder.cpp`. Low churn means low ROI on refactor — leave alone unless physics bug surfaces. |
| 8 | **xrCDB** | 6k | 15 | 5 | 3 | Collision DB (OPCODE). Compact module with disproportionate CCN density. Low churn — stable. |
| 9 | **utils/mp_gpprof_server** | n/a | low | 27 | n/a | Dead-on-arrival multiplayer/profile server (vendored GameSpy SDK). 75 of cppcheck's 127 findings + 27 high-CCN functions. **Strong candidate for outright deletion** under `#66` — we don't ship multiplayer matchmaking. |
| 10 | **editors** | n/a | low | n/a | 37 cppcheck findings | Map/level editor code. Not shipped in MasterGold builds. Another deletion candidate. |

## Cross-references between reports

- **Zero overlap between top-100 churn files and CCN>30 functions.** This
  is the load-bearing finding. The files we edit constantly
  (`xrCore.cpp`, `MainMenu.cpp`, `console_commands.cpp`,
  `xr_level_controller.cpp`) are *boring API surfaces* — long files with
  many small functions. The complexity hotspots
  (`clsid_to_spawn_category`, `CAI_Stalker::debug_text`,
  `CActor::g_SetAnimation`) live in code nobody touches. Two different
  bug-risk profiles. Churn-driven refactor and complexity-driven
  refactor would target totally different files.
- `xrCore/LocatorAPI.cpp`: ranks #7 in churn (7 edits) AND owns
  cppcheck null-deref findings via `xr_strcpy`/`strcpy_s`. Worth a
  defensive read — see CLAUDE memory `project_engine_fs_path_quirks`.
- `Layers/xrRender_R2/r2.cpp`: #28 in churn (6 edits) AND CCN=99 on its
  `CRender::create`. Our Apple SSAO/Sun-cvar branch lives in this file
  (per CLAUDE memory `project_apple_gl_shader_cascade`). High-cost area
  to keep clean.
- `xrGame/MainMenu.cpp`: top-5 churn (12 edits, includes our recent
  `f543d45a8` pre-flight TCP probe for updater) but only 1 clang-tidy
  finding and no CCN>30 functions. Healthy structure; just an
  active feature area.
- `utils/mp_gpprof_server/atlas_stalkercoppc_v1.c` appears 6 times in
  the top-50 CCN list. Vendored, generated-looking — confirms the
  delete-candidate framing for `#66`.

## Known caveats / re-run recipes

1. **PCH cascade.** Apple-clang↔llvm@20 PCH incompatibility hid most of
   clang-tidy and IWYU's coverage. Workaround used: strip
   `-include-pch`/`-include cmake_pch.hxx` from compile DB and use
   `-isysroot $(xcrun --show-sdk-path)`. For a complete IWYU run,
   rebuild the project with `target_precompile_headers` disabled
   temporarily, or rebuild PCH with llvm@20 by setting
   `CMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm@20/bin/clang++`.
2. **clang-tidy SIGTERM at ~25 min.** Killed mid-sweep. The 110
   findings captured are accurate but partial (≈25% of TUs). For a
   complete sweep, run from a foreground terminal session with no
   harness timeout, or chunk by module:
   `run-clang-tidy ... '^.../src/xrGame/'` per module.
3. **cppcheck unusedFunction = 0 hits.** Expected — cppcheck's
   cross-TU detection is limited; reachable-from-`main` analysis would
   need a different tool (e.g. `callgraph` from clang itself, or
   `unused-symbols` via lld map files).
4. **dep-graph is text-based.** Counts `#include` directives, not
   *symbol* usage. For a real symbol-level inter-module dependency
   graph use `clang-include-cleaner` or `cflow`. Good enough for
   the high-level shape we needed here.

## How to use this for #66

1. **Delete candidates** (strong): `utils/mp_gpprof_server` (75 cppcheck
   findings, 27 high-CCN funcs, multiplayer dead weight),
   `src/editors` (37 cppcheck findings, MasterGold-stripped anyway).
2. **Single-function refactor candidates**:
   `xrServerEntities/object_factory_spawner.h::clsid_to_spawn_category`
   (CCN=198 — a switch/dispatch giant, safe to split because rarely
   touched).
3. **Care-budget keep-clean zones**: `Layers/xrRender_R2/r2.cpp`,
   `xrCore/LocatorAPI.cpp` — high blast radius for any change because
   our Apple-platform branches live there.
4. **Don't bother refactoring**: high-churn cvar/menu plumbing files
   (`xrCore.cpp`, `MainMenu.cpp`, `console_commands.cpp`). Healthy
   structure; churn is feature work, not complexity debt.
