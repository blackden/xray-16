# Engine Map — OpenXRay touchpoints

Живой индекс «где живёт что» в движке. Назначение: повторяющиеся
«где X?» вопросы кладутся сюда **после** первой exploration, чтобы
следующая сессия начиналась без re-discovery. Любой нетривиальный поиск
(≥3 файлов, ≥1 Explore-агент) должен оставлять след тут.

Группировка — по слоям движка, не по фичам. Линки `file:line` валидны
на момент записи; при смене номеров обновлять при следующей встрече.

---

## Identity (Core.UserName, CD-key, player nick)

- POSIX username lookup: `src/xrCore/xrCore.cpp:428-450` (uses
  `pw_name`, with `USER`/`LOGNAME` env fallbacks, finally literal
  `"player"`).
- Windows username lookup: `src/xrCore/xrCore.cpp:421-427`
  (`GetUserName` via WinAPI).
- Global storage: `Core.UserName` (string64), declared in
  `src/xrCore/xrCore.h:119`.
- Player nick (CD-key): `src/xrGame/ui/UICDkey.cpp:230-258`.
- Downstream readers (everything that prefixes files with the
  username):
  - Log filename: `src/xrCore/log.cpp:222,226`.
  - Save filename: `src/xrGame/Level_input.cpp:253`.
  - Autosave: `src/xrGame/alife_update_manager.cpp:192`.
  - Screenshot prefix: `src/Layers/xrRenderDX11/dx11r_screenshot.cpp:71,87`.

## Renderer init (GL on Apple)

- Engine→Render entry: `src/xrEngine/Device_create.cpp:47`
  (`GEnv.Render->OnDeviceCreate(fname)`). After this call: GL context,
  default VAO, framebuffers ready; no level loaded yet.
- D3DXRenderBase::OnDeviceCreate body:
  `src/Layers/xrRender/D3DXRenderBase.cpp:130-164` (compiled per
  renderer; defines `RENDER == R_GL` on PC_GL target).
- GL device creation: `src/Layers/xrRenderGL/glHW.cpp:94`
  (`CreateDevice`).
- Default VAO bind: `src/Layers/xrRenderGL/glHW.cpp:153`.
- Apple-specific guards (examples for the pattern):
  - `src/Layers/xrRenderGL/glTexture.cpp:137` (immutable storage 3D).
  - `src/Layers/xrRenderGL/glSH_RT.cpp:36`
    (`GL_MAX_TEXTURE_SIZE` vs `GL_MAX_FRAMEBUFFER_*` — Apple 4.1 lacks
    the framebuffer-specific queries).

## Shader compile (GL)

- Core compile (template): `src/Layers/xrRender/ShaderResourceTraits.h:44-90`
  (`GLCompileShader<type>`).
- Error log line `! shader compilation failed:`:
  `src/Layers/xrRender/ShaderResourceTraits.h:30`
  (`show_compile_errors`).
- Permutation+define plumbing — `src/Layers/xrRenderPC_GL/rgl_shaders.cpp`:
  - `shader_options_holder` (the `#define …` accumulator) at lines
    87-111.
  - `shader_name_holder` (the numeric-hash filename you see in logs,
    e.g. `accum_sun_nomsaa.ps\125601…0001`) at lines 57-85.
  - `shader_sources_manager` (file loader + `#include` injector) at
    lines 113-197.
  - `CRender::shader_compile`: lines 198-590 (entry point for all
    shader builds).
- SSAO cvar → shader-option mapping: `src/Layers/xrRender_R2/r2.cpp:381-394`
  (DX/OGL/Apple branches).
- Shader-option define injection (the critical part): `src/Layers/xrRenderPC_GL/rgl_shaders.cpp:348-403`
  — all `*_QUALITY` macros (SSR, SUN_SHAFTS, SSAO, SUN) must be
  defined unconditionally because Apple GL 4.10 parser rejects `#if`
  on undefined identifiers (the "incorrect preprocessor directive"
  cascade). Pattern: define as 0 when feature off, never skip.

## Screenshots / save thumbnails

- Capture entry (engine-side): `IRender::Screenshot(mode, name)` in
  the IRender interface; called from `MainMenu.cpp:554`,
  `autosave_manager.cpp:75`, `console_commands.cpp:707` with
  `SM_FOR_GAMESAVE` + a `<savename>.dds` path. Mode enum:
  `src/xrEngine/Render.h:152`.
- DX11 implementation (reference): `src/Layers/xrRenderDX11/dx11r_screenshot.cpp:38-66`
  — DirectXTex CaptureTexture → Resize → BC1 Compress → SaveToDDSMemory.
- GL implementation: `src/Layers/xrRenderGL/glr_screenshot.cpp:67-148`
  — glReadPixels → Y-flip → `imf_Process(... imf_box)` to 128×128 →
  inline 128-byte uncompressed-RGBA8 DDS header. gli reads it
  (`glTexture.cpp:111`).
- Loader (display side): `src/Layers/xrRender/Texture.cpp:280` checks
  `$game_saves$` for `<name>.dds`.

## Rain emission / wet-shader

- Frame logic + hemi_factor smoothing: `src/xrEngine/Rain.cpp:117-179`.
  `m_hemi_factor` is the camera-relative sky visibility, exposed via
  `CEffect_Rain::get_hemi_factor()` (`src/xrEngine/Rain.h`).
- Three read sites of `rain_density` (all gated by smoothstep over
  hemi_factor as of 2026-05-19):
  - Streak emission: `src/Layers/xrRender/dxRainRender.cpp:45-70`.
  - Wet-shader uniform: `src/Layers/xrRender_R2/r3_rendertarget_draw_rain.cpp:5-25`.
  - Shadow rain factor: `src/Layers/xrRender_R2/r3_R_rain.cpp:51-78`.
- Pattern when adding a new rain-driven effect: read
  `Environment().CurrentEnv.rain_density`, multiply by
  `smoothstep(0.05, 0.25, eff_Rain->get_hemi_factor())` before use.

## Cvars (renderer)

- Registration table: `src/Layers/xrRender/xrRender_console.cpp:920-926`
  for SSAO family (`r2_ssao_mode`/`r2_ssao`/`r2_ssao_blur` etc.).
- Storage backings (declared in `src/Layers/xrRender/xrRender_console.h`):
  - `ps_r_ssao` (u32, line 82).
  - `ps_r2_ls_flags_ext` (Flags32, line 186).
  - `R2FLAGEXT_SSAO_*` enum: same header.
- Setter API: no public setter — variables are mutated either by
  reading from `user.ltx` at boot (via console `Execute`) or by
  invoking `Console->Execute("r2_ssao", "off")` at runtime.

## FS / paths / launch

- Apple appdata override (`-overlaypath`): `src/xrCore/LocatorAPI.cpp:1041-1055`
  — parses `sscanf("%[^ ] ", …)`, so the path must not contain spaces.
- POSIX path separator normalisation: same file, `rescan_path` and
  `_set_root` helpers (use them — they're the entry points where the
  separator audit lives).
- Safe-mode sentinel: `~/.openxray-data/_appdata_/.boot_in_progress`.
  - Cleared by engine after `STABLE_BOOT_FRAMES` (120): see
    `src/xrEngine/x_ray.cpp:391-479`.
  - Dropped + checked by launcher: `scripts/mac/package_app.sh:235`.
  - Behaviour when sentinel present at launch: graphics reset to
    minimum (1280x720, AA/sun/DOF off, supersample 0). Used as
    recovery from any hang or crash that happened mid-level-load.

## Input / keybinds

- Key map (key name → key code): `src/xrEngine/xr_level_controller.cpp:225`.
- macOS-friendly subset and rebind syntax: `notes/cheatsheet.md`.
- F6/F7 (dev_tools-gated hotkeys): see memory
  `project_dev_tools_gate.md` for the gating pattern.

## Recurring patterns (look here before inventing)

- Apple-platform code gates: `#if defined(XR_PLATFORM_APPLE)` in
  GL renderer files. Examples: `glTexture.cpp:137` (immutable
  storage), `glSH_RT.cpp:36` (max-texture query),
  `xrRender_R2/r2.cpp:381` (SSAO gate).
- USE_OGL gate in shared-render code: `r2.cpp` has multiple
  `#elif defined(USE_OGL)` branches mirroring the DX11 path with a
  reduced feature set — the right place to add new OGL fallbacks.
- Cvar registration sits in `xrRender_console.cpp` or
  `xr_*_cmds.cpp`; storage globals in matching `*_console.h`.
- One-time warning idiom: `static bool warned = false; if (!warned) { Msg("…"); warned = true; }`
  — see new code in `r2.cpp:391-400`.

## Open questions / next-time-investigate

These are touchpoints we expect to want next session but haven't
mapped yet — fill in when first touched:

- Smart-terrain job assignment + reload-state restore (for the NPC-
  frozen-in-column observation).
- ~~Flashlight / dynamic-source lighting on OGL~~ — **closed
  2026-05-19**: `accum_omni_*_nomsaa.ps` sat in the same `*_QUALITY`
  undefined-macro cascade as SSAO; the `rgl_shaders.cpp` unconditional-
  defines fix healed it. No separate OGL light-gate exists. Lesson:
  if multiple GL/shader symptoms appear together on Apple, suspect a
  single preprocessor cascade before chasing N independent bugs.
- Dialog state machine (`dialog_manager.script` and how pre/post
  conditions on dialog nodes are evaluated) — requires unpacked
  `resources.db?` scripts.
