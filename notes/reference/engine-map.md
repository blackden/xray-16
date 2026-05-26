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

- Frame logic + hemi_factor smoothing: `src/xrEngine/Rain.cpp:117-180`.
  `m_hemi_factor` is the fraction (0..1) of 5 upward raycasts from the
  camera that hit open sky, time-smoothed by `fTimeDelta`. Exposed via
  `CEffect_Rain::get_hemi_factor()` (`src/xrEngine/Rain.h`).
- **Why not `get_luminocity_hemi_cube()`** (2026-05-20 lesson):
  `hemi_cube_smooth` is polluted by dynamic-light contribution
  (`LightTrack.cpp:260-262` adds `hemi_cube_light` from nearby lamps),
  so under lit interiors (Yanov station) the gate never fires. A direct
  `ObjectSpace.RayTest(... rqtStatic ...)` straight up is the
  unambiguous probe. Marked with `XXX HEMI_LIGHT_POLLUTION` in
  `LightTrack.cpp` for any future caller that needs pure sky.
- Three read sites of `rain_density` — all gated by
  `smoothstep(0.2, 0.6, m_hemi_factor)`:
  - Streak emission: `src/Layers/xrRender/dxRainRender.cpp:45-65`.
  - Wet-shader uniform: `src/Layers/xrRender_R2/r3_rendertarget_draw_rain.cpp:5-22`.
  - Shadow rain factor: `src/Layers/xrRender_R2/r3_R_rain.cpp:52-68`.
- Pattern when adding a new rain-driven effect: read
  `Environment().CurrentEnv.rain_density`, multiply by
  `smoothstep(0.2, 0.6, eff_Rain->get_hemi_factor())` before use.
  Keep the gate range in sync across all sites (see XXX
  `RAIN_GATE_SYNC` comment in `dxRainRender.cpp`).

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

- Apple appdata override (`-overlaypath`): `src/xrCore/LocatorAPI.cpp:1041-1090`
  — parses `sscanf("%[^ ] ", …)`, so the path must not contain spaces.
  Re-roots `$logs$` and `$app_data_root$`, then **walks `m_paths` and
  re-roots any FS_Path whose `m_Root` was derived from the old
  `$app_data_root$`** (children like `$game_saves$`, `$screenshots$`,
  `$downloads$`). Without that walk children silently stay under
  `$fs_root$/_appdata_/`.
- `FS_Path` structure: `src/xrCore/LocatorAPI_defs.h:14-40`. Stores
  `m_Root` + `m_Add` + resolved `m_Path` as strings; **no parent
  back-reference**. Resolved at fsgame.ltx parse time (see
  `LocatorAPI.cpp:1005-1018` for parent-alias lookup pattern), then
  static — re-rooting the parent does **not** auto-update children.
- `FS_Path::_set_root`: `src/xrCore/LocatorAPI_defs.cpp:83-104`.
  Updates `m_Root`, then recomputes `m_Path = m_Root + m_Add` with
  trailing `_DELIMITER`. Calls `restore_path_separators` internally,
  so input may be POSIX form. Does **not** touch `m_files` or any
  other FS_Path object — that's `rescan_path`'s job.
- `CLocatorAPI::m_paths`: `xr_map<pcstr, FS_Path*, pred_str>` (see
  `LocatorAPI.h:147`). Safe to iterate while calling `_set_root` /
  `rescan_path` — neither inserts/erases in the map.
- `CLocatorAPI::rescan_path`: `src/xrCore/LocatorAPI.cpp:1942-1976`.
  Removes m_files entries under prefix, then `Recurse(full_path)` to
  re-scan disk. Tolerates non-existent directories (the comment at
  line 1944 documents the historical "early-return on lower_bound ==
  end()" bug that left programmatically-added paths unindexed).
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

## macOS AppKit lifecycle (sleep/wake/focus)

PR #115 (gitea #114, A.1 native-shell roadmap): own `NSApplicationDelegate`
ловит NSWorkspace и focus events; AppKit notifications **никогда не**
вызывают engine pause/activate напрямую — race с render thread.

- Delegate wrapper class: `src/xrEngine/macos_cocoa_shim.mm`
  (`OpenXRayCocoaShim`). Wrappит SDL'овский delegate через
  `forwardingTargetForSelector:`; наши собственные методы Obj-C runtime
  находит первыми, остальное форвардится в SDL.
- Observers regsubscribe: `OpenXRay_ArmLifecycleObservers()` —
  вызывается из финала `CRenderDevice::Create()`
  (`src/xrEngine/Device_create.cpp`), **не** из install hook'а. Иначе
  spurious sleep firing до boot complete.
- NSWorkspace subscriptions: `NSWorkspaceWillSleepNotification` +
  `NSWorkspaceDidWakeNotification`. NSApplicationDelegate methods:
  `applicationDidBecomeActive:` + `applicationWillResignActive:` (с
  `[sdlDelegate performSelector:_cmd]` форвардом — без него ломается
  SDL focus tracking).

**Three-stage decoupling** (AppKit main thread vs render thread race):

1. Observer fires → `extern "C" OpenXRay_On{SystemWillSleep,SystemDidWake,AppDidBecomeActive,AppWillResignActive}()`
   в `src/xrEngine/Engine.cpp` — атомарно ставит `PendingLifecycleEvent`
   enum (`std::atomic<PendingLifecycleEvent>`, файловая статика).
2. Drainer `OpenXRay_ApplyPendingLifecycleEvent()` (`Engine.cpp`)
   вызывается **первой строкой** `CRenderDevice::ProcessFrame`
   (`device.cpp`). Под `XR_PLATFORM_APPLE` only.
3. Drainer переводит event → `Device.Pause(TRUE/FALSE, TRUE, TRUE, "system will sleep")`
   или `Device.OnWindowActivate(m_sdlWnd, true/false)` — через
   существующие paths. Idempotency guard: `if (!Device.b_is_Active)` /
   `if (Device.b_is_Active)` — collapse'ит SDL-originated focus event
   и наш Cocoa-originated в одну транзицию.

**Pattern для добавления новых AppKit observers:** копируй pipeline.
Не зови `Device.*` синхронно из ObjC — только atomic flag + apply at
frame boundary.

## ghttp dispatch worker (gitea #117)

PR follow-up к #115: ghttp/gsCore polling больше не бежит на main thread.
Тот же drain-pattern что и AppKit lifecycle (atomic-flag flush на frame
boundary), но для асинхронной библиотеки (ghttp), а не для Cocoa observer'ов.

- **Worker queue + completion drain (module xrGameSpy)**:
  `src/xrGameSpy/ghttp_worker_apple.mm`. Serial `dispatch_queue`
  (`tech.fedorov.openxray.ghttp.worker`, QoS user-initiated). API:
  - `OpenXRay_GhttpInstallWorker` / `OpenXRay_GhttpShutdownWorker`
    (idempotent install, bounded by StartUp→CleanUp pair).
  - `OpenXRay_GhttpDispatchAsync(fn)` / `OpenXRay_GhttpDispatchSync(fn)`
    — marshal arbitrary work onto the worker.
  - `OpenXRay_GhttpEnqueueCompletion(fn)` — push completion record onto
    `std::mutex` + `std::deque<GhttpCompletion>`.
  - `OpenXRay_GhttpDrainCompletions` — main-thread drain (swap-out under
    mutex, invoke without lock held).
  - `OpenXRay_GhttpDiscardPendingCompletions` — teardown safety: drop
    stranded records без invoke() (FastDelegate `this` pointer уже dead).
  - `OpenXRay_GhttpAssertOnWorkerQueue` (DEBUG-only) — uses
    `dispatch_queue_set_specific` + `dispatch_get_specific` (НЕ deprecated
    `dispatch_get_current_queue`).
- **Hook slot + aggregator (module xrEngine)**: `src/xrEngine/Engine.cpp:248-264`.
  `OpenXRay_RegisterGhttpDrainHook(fn)` хранит function-pointer
  атомарно; `OpenXRay_RunPerFrameMacOSHooks` зовёт lifecycle drain THEN
  ghttp drain (порядок — invariant: lifecycle pause может suppress UI
  работу которую ghttp completions триггернули бы).
- **Per-frame hook fire**: `src/xrEngine/device.cpp:280-288`,
  первой строкой `CRenderDevice::ProcessFrame`, Apple-only.
- **Call-site routing**: `src/xrGameSpy/GameSpy_HTTP.cpp` под
  `XR_PLATFORM_APPLE` — `Think`, `DownloadFile`, `FetchString`,
  `StopDownload` все идут через `OpenXRay_GhttpDispatchAsync`; success/
  error/early-fail callbacks пушат `OpenXRay_GhttpEnqueueCompletion`.

**Cross-module link edge (Apple-only)**: xrGameSpy → xrEngine для
`OpenXRay_RegisterGhttpDrainHook`. xrGameSpy сидит downstream от
xrEngine в обычной dependency tree (xrGame depends on both); под
`BUILD_SHARED_LIBS=ON` каждая lib линкуется standalone, поэтому
`src/xrGameSpy/CMakeLists.txt` имеет explicit `target_link_libraries`
на xrEngine под APPLE-guard. См. комментарий «link-deps» в этом
CMakeLists. Static-lib build резолвит через xr_3da link order.

**Pattern для нового async/observer source'а**: copy pipeline.
1. Source (NSWorkspace notification / dispatch worker / GCD source) →
2. atomic flag или mutex-protected queue в Engine.cpp / .mm файле →
3. extern "C" register-hook surface если cross-module →
4. frame-boundary drain в `OpenXRay_RunPerFrameMacOSHooks`
   (`device.cpp:280-288`) →
5. main-thread sink (Device.*, UI dialog, sound, etc.).
Никогда не call'ить main-thread engine API синхронно из observer/worker.

**Teardown invariant (cpp-engineer audit, #117 commit 4)**:
- Step 1: unregister drain hook (atomic store).
- Step 2: `dispatch_sync` ghttpCleanup на worker (drains every enqueued
  Think/Save/Get; ghttp internals torn down on the same thread that
  touched them).
- Step 3: discard pending completions (FastDelegate targets могут быть
  about-to-be-destructed; drop без invoke()).
- Step 4: shutdown worker (sync barrier + release queue).
Этот ordering должен соблюдаться во всех будущих teardown paths
производных от этого паттерна — race window между worker и main =
UAF source.

## Shutdown / quit sequence

Three entry points all converge on the same deferred-event sequence:

- Console `quit` command (`CCC_Quit::Execute`): `src/xrEngine/xr_ioc_cmd.cpp:62-76`
  — also reached via pause-menu "Quit to Windows" and main-menu's
  Quit button.
- SDL window-close button (red traffic light, Cmd+W on some configs):
  `src/xrEngine/device.cpp:380-389` (`SDL_WINDOWEVENT_CLOSE` case).
- macOS Cocoa Cmd+Q + Apple menu Quit: `src/xrEngine/macos_cocoa_shim.mm`
  (`handleQuitRequest:`) → `OpenXRay_RequestGracefulQuit` in
  `src/xrEngine/Engine.cpp:124-132` (Apple-only C-linkage glue, written
  so the `.mm` doesn't need to include xrCore headers, which collide
  with Foundation types under ObjC++).

All three set `g_bShuttingDown = true` (declared in
`src/xrCore/xrCore.h:144-151`, defined in `src/xrCore/xrCore.cpp:33`)
and then `Engine.Event.Defer("KERNEL:disconnect")` + `Defer("KERNEL:quit")`.

Save & Exit / "return to main menu" UI handlers defer **only**
`KERNEL:disconnect` — they keep the engine alive for the next level
load. They must not set the flag.

**Event handler chain on next frame:**
- `IGame_Persistent::OnEvent(eDisconnect)` (`src/xrEngine/IGame_Persistent.cpp:284-304`)
  → `g_pGameLevel->net_Stop()` + `DestroyLevel(g_pGameLevel)` → `~CLevel`
  → `~IGame_Level` (`src/xrEngine/IGame_Level.cpp:34-55`) →
  `GEnv.Render->level_Unload()` (`src/Layers/xrRender_R2/r2_loader.cpp:111-200`,
  cleans `Lights`, `HOM`, `Details`, `Visuals`, VBs/IBs, etc.) +
  `GEnv.Sound->destroy_scene(Sound)` → `~CSoundRender_Scene`
  (`src/xrSound/SoundRender_Scene.cpp:10-44`).
- `CEngine::OnEvent(eQuit)` (`src/xrEngine/Engine.cpp:100-115`):
  `pInput->GrabInput(false)` + `SDL_PushEvent(SDL_QUIT)` → main loop
  exits.

**Process teardown (`CApplication::~CApplication`, `src/xrEngine/x_ray.cpp:331-378`):**

  Order matters and has been a foot-gun:
  1. `g_pGamePersistent->OnAppEnd()` (line 336).
  2. `destroy_persistent(g_pGamePersistent)` (line 339) — **destroys
     CGamePersistent which owns `SpatialSpace` (the `ISpatial_DB`
     used by every `SpatialBase::spatial_register/unregister`)**.
  3. ...input / settings / console / sound / etc.
  4. `Device.Destroy()` (line 354) → `CRenderDevice::Destroy`
     (`src/xrEngine/Device_destroy.cpp:6-37`) → `GEnv.Render->FlushGpuQueue()`
     (Apple-only glFinish) → cascade through `D3DXRenderBase::OnDeviceDestroy`
     → `CRender::destroy()` (`src/Layers/xrRender_R2/r2.cpp:526-547`) →
     `CHW::DestroyDevice()` (`src/Layers/xrRenderGL/glHW.cpp:182-199`).

  **Foot-gun**: `CRender::destroy()` runs AFTER step 2 — by then
  `ISpatial_DB` is dead memory. Anything that calls
  `spatial_unregister()` from this point (including `Lights.Unload()`
  or `~CLight_DB` during static destruction) will deadlock on the
  destroyed `m_lock` mutex (`pthread_mutex_lock` hangs forever on
  macOS pthread). Cleanup of `SpatialBase`-derived objects (lights,
  npcs, etc.) MUST happen during `level_Unload`/`net_Stop` while
  `CGamePersistent` is still alive.

  The `g_bShuttingDown` fast-quit flag does NOT solve this — it
  enables `~CSoundRender_Scene` to skip per-emitter destruction at
  exit (saving ~2.5s on a CoP level), but guarding
  `spatial_unregister` itself with the flag leaves stale entries in
  the octree → subsequent `q_box` queries (e.g. from physics during
  `CObjectList::Update`) hit dangling pointers and walk into
  infinite recursion. **The right fix is order, not guards.** See
  gitea #49 commit history for the false-start.

**Quick canary if Cmd+Q regresses again:**
- Grep the engine log for `level_Unload: g_pGameLevel=... b_loaded=...
  sun=...` (one line on disconnect entry, `r2_loader.cpp`). Absence =
  disconnect path skipped level cleanup.
- Grep for `! ~CLight_DB at process exit with live lights` — fires only
  if lights survived into static destruction; means
  `~IGame_Level → level_Unload → Lights.Unload` didn't run.
- Grep for `==> teardown[snd]: ~CSoundRender_Scene SKIP emitter cleanup
  (g_bShuttingDown=1)` — confirms fast-quit path engaged.
- `make sample-hang` (Debug toolkit, below) on the wedged xr_3da gives
  a C stack trace that disambiguates the wedge point quickly
  (event-pump inside disconnect vs. static destruction vs.
  `Engine.Destroy()` tail).

**Anti-pattern (do NOT do this):** guard `SpatialBase::spatial_unregister`
by `g_bShuttingDown`. Tried in the #49 false-start: the guard skipped
the actual removal from the octree, leaving dangling pointers; later
`q_box` queries from physics destruction during disconnect walked the
broken tree into infinite recursion. If you ever consider this — you
are masking destruction order, not fixing it. Find the right time to
unregister while the spatial DB is still alive (e.g. via
`level_Unload`), don't no-op the unregister itself.

## Static destruction cascade hazards (gitea #52, post-#49)

After #49 closed the in-`main()` deadlock, a second-order hang surfaced
during C++ static destruction (after `main()` returns). Sample stack is
deterministic across Cmd+Q AND Cmd+W:

```
exit → __cxa_finalize_ranges
  → CRender::~CRender  (RImplementation global at r2.cpp:20)
    → ~vector<resptr<Shader>>  (CRender::Shaders, r2.h:311)
      → cascade per element: ~Shader → ~ShaderElement → ~SPass
        → ~STextureList → ~pair<uint, resptr<CTexture>> → ~CTexture
          → CResourceManager::_DeleteTexture(this)
            → std::map::find → __tree::__root  ← 2-sec spin
```

**Root cause:** `D3DXRenderBase::Destroy()` (`D3DXRenderBase.cpp:76-80`)
does `xr_delete(Resources); HW.DestroyDevice();` — but does NOT clear
engine-side ref containers (`CRender::Shaders`, `Visuals`, `Lights`)
first. After `Destroy()` returns and static destruction starts, those
containers cascade through `_DeleteTexture` → `m_textures.find` on
torn-down state. macOS `std::map::find` on freed memory spins on
`__tree::__root` instead of crashing.

**Member declaration order in CResourceManager matters:**
`ResourceManager.h:52` declares `map_Texture m_textures`. Line 100
declares `CScriptEngine ScriptEngine`. C++ reverses for destruction:
**ScriptEngine destructs FIRST**. So `~CScriptEngine` → `lua_close` →
Lua userdata refs drop → cascade lands on still-alive `m_textures`.
Reordering breaks this. Do not reorder.

**Quit-path unification confirmed** (diagnostic 2026-05-22):
- Cmd+Q via Cocoa shim → `OpenXRay_RequestGracefulQuit` (`Engine.cpp:127`)
- Cmd+W via SDL_WINDOWEVENT_CLOSE (`device.cpp:380-392`)
- Console `quit` via `CCC_Quit::Execute` (`xr_ioc_cmd.cpp:62-77`)
- Apple menu Quit → Cocoa shim same as Cmd+Q

All three set `g_bShuttingDown = true; Defer(KERNEL:disconnect); Defer(KERNEL:quit)`.
They are structurally equivalent — race lives *after* this teardown
unifies, in static destruction phase, not in path divergence. **Single
helper `Engine::RequestGracefulShutdown()` is the obvious DRY win**
(planned commit 4 of #52).

**Two distinct shutdown flags — don't confuse:**
- `g_bShuttingDown` (`xrCore.h:144-151`) — fast-exit hint, set at Cmd+Q
  time before eDisconnect. Used by sound to skip per-emitter destruction.
- `g_bStaticDestruction` (`xrCore.h:152`, added 2026-05-22) — strict
  post-main marker, set in atexit lambda in `entry_point.cpp`. Used as
  guard in `SpatialBase::spatial_unregister` (`ISpatial.cpp:82`) and
  `reclaim<T>` template (`ResourceManager.h:273`). For static-destruction
  safety, use the latter; the former is for fast-quit pruning.

**Vector vs map-based `_Delete*`:**
- Vector-based (`reclaim<T>`-routed): `_DeleteElement`, `_DeletePass`,
  `_DeleteTextureList`, `_DeleteConstantList`, `_DeleteMatrixList`,
  `_DeleteState`, `_DeleteDecl`, `_DeleteConstantTable`, `DeleteGeom`.
  Guarded today by `reclaim<T>` early-return on `g_bStaticDestruction`.
- Map-based (direct `std::map::find`): `_DeleteTexture`, `_DeleteRT`,
  `_DeleteMatrix`, `_DeleteConstant`, `_DeletePP` (GL), `DestroyShader<T>`
  (VS/PS/GS/HS/DS/CS via template). NOT guarded today — gitea #52
  adds layer-3 defense-in-depth guards.

**Primary fix design (gitea #52, in-progress):**
1. Layer 1 — `D3DXRenderBase::Destroy()` calls new virtual hook
   `DrainEngineRefs()` BEFORE `xr_delete(Resources)`. Override in
   `CRender` clears `Shaders`, `Visuals`, `SWIs`, `VB/IB`, `nDC/xDC`.
   Cascade lands on still-alive `m_textures` → find+erase succeed.
2. Layer 2 — `~CResourceManager` member declaration order (ScriptEngine
   first, m_textures last) ensures Lua cascade lands on alive map.
3. Layer 3 — `g_bStaticDestruction` guards in 6 map-based `_Delete*` +
   existing `reclaim<T>` and `spatial_unregister` guards as defense-in-depth.

**What `DrainEngineRefs` MUST NOT do:** call `Lights.Unload()`. By the
time `D3DXRenderBase::Destroy()` runs, `~CApplication` has already done
`destroy_persistent(g_pGamePersistent)` at `x_ray.cpp:340` →
`ISpatial_DB` is dead → `Lights.Unload()` → `~CLight` →
`spatial_unregister` → dead pthread mutex → hang. The existing
spatial_unregister guard handles persistent lights at static destruction;
just don't re-trigger the deadlock from Destroy().

**What `DrainEngineRefs` MUST NOT call:** `PSLibrary.OnDestroy()` — already
invoked in `CRender::destroy()` at `r2.cpp:543` (which runs from
`OnDeviceDestroy` BEFORE `Destroy`). Double-call is wasted.

**Persistent (non-level) refs that survive level_Unload:**
- Lua userdata holding `ref_shader`/`ref_texture` (released only at
  `lua_close` inside `~CScriptEngine`)
- HUD / fonts / console UI shaders (no explicit teardown)
- CRenderTarget's 81 shaders + 8 geometries + 16+ RT pool entries
  (released at `xr_delete(Target)` in `CRender::destroy()`)
- CLight_DB's 44 v_static + 363 v_hemi lights (per POSTLOG canary —
  survive level_Unload, hit spatial_unregister guard at static destruction)

These are why Cmd+Q from main menu (no level loaded, `level_Unload`
never runs) still has populated containers at static destruction time.

## Debug toolkit (use these, don't reinvent)

One-stop reference for instrumentation and diagnostic tools.

- **`make sample-hang`** — sample the currently-hung xr_3da, output to
  `~/Downloads/sample-TIMESTAMP.txt`. Calls `scripts/mac/sample-hang.sh`.
  Run with `sudo make sample-hang` if the plain command fails (SIP
  strict / hardened-runtime). Use when Cmd+Q wedges, when the engine
  hangs mid-level, or any time you want a C stack of all threads.
- **`src/Common/PostLogMark.hpp`** — `POSTLOG_MARK("tag")` writes
  `==> postlog@<ms>: <tag>\n` to stderr with a monotonic timestamp.
  Works even AFTER `CloseLog()` (Core._destroy), unlike `Msg`. Use for:
  - C++ static destructors (run after `main` returned, log closed).
  - Post-cascade shutdown phase (Engine.Destroy → SDL_Quit → atexit).
  - Any place where stderr is the only viable channel.
  No xrCore dependencies, safe to include from `.mm` files.
  Don't use when Msg/Log are working — Msg has timestamps + level
  filtering + remote-debug visibility.
- **Dual log paths (don't confuse them):**
  - Engine log: `~/.openxray-data/logs/openxray_ragnar.log` —
    driven by `Msg`/`Log`. Closes at `Core._destroy()`.
  - Launcher stdout+stderr capture: `~/Library/Logs/OpenXRay/openxray.log`
    — driven by `>> ... 2>&1` redirect in
    `dist/OpenXRay.app/Contents/MacOS/OpenXRay` (the launcher shell).
    Keeps capturing AFTER the engine log closes — POSTLOG_MARK,
    atexit, static-destructor stderr output lands here.
  Grep both when investigating shutdown / post-cascade behaviour.
- **Build config inspection:**
  `grep -E "XRAY|TRACY|USE_OGL|USE_ASAN" build/CMakeCache.txt`.
  Quick check what's compiled in. We currently ship with
  `XRAY_ENABLE_TRACY:BOOL=OFF` (verified 2026-05-21).
- **Existing diagnostic markers in the codebase:**
  - `entry_point.cpp:140` — `==> main returning with code %d` (just
    before main returns).
  - `entry_point.cpp:91` (Apple-only) — `==> ATEXIT fired` (post-main,
    pre-static-destructors).
  - `Light_DB.cpp ~CLight_DB` — fires `! ~CLight_DB at process exit
    with live lights ...` only if light vectors survived into static
    destruction (regression canary for #49-class wedges).
  - `Device_destroy.cpp` — `Destroying Render...` / `Render destroyed.`
    bracket the visible engine teardown.

## Module dependency tree

Validated from `target_link_libraries` (2026-05-21). Use this when
deciding where to put a new global / typedef / utility — put it in
the **lowest** module where it is needed, to keep the diamond shape
manageable.

```
xrMiscMath                                  (foundation)
xrCore       ← xrMiscMath (priv), SDL2 + pthread (pub)
xrAPI        ← xrCore base (mostly interface-only)
xrCDB        ← xrCore + xrMiscMath + xrAPI + xrOPCODE
xrMaterialSystem ← xrMiscMath (pub) + xrAPI + xrCore
xrSound      ← xrCore + xrMiscMath + xrAPI + xrCDB
              + xrMaterialSystem + OpenAL + Ogg + Vorbis
xrScriptEngine ← (Lua VM, used by xrEngine and xrGame)
xrNetServer  ← (multiplayer net, used by xrEngine and xrGame)
xrEngine     ← xrMiscMath (pub) + xrAPI + xrCDB + xrMaterialSystem
              + xrCore + xrSound + xrScriptEngine + xrNetServer
              + xrImGui + OpenAL + Ogg + Theora + SDL2
xrAICore     ← xrCore + xrEngine + xrMiscMath + xrAPI + xrScriptEngine
xrGame       ← xrEngine + everything above
```

Practical examples:
- `g_bShuttingDown` lives in `xrCore` because both `xrSound`
  (`~CSoundRender_Scene` fast-path) and `xrEngine` (set points) need
  it. Lowest common ancestor.
- `POSTLOG_MARK` lives in `Common/` (header-only, no module) because
  it is meant to be included from anywhere including `.mm` files and
  static destructors of low-level objects — even from xrCore itself
  during its own teardown.

## Input / keybinds

- Key map (key name → key code): `src/xrEngine/xr_level_controller.cpp:225`.
- macOS-friendly subset and rebind syntax: `cheatsheet.md` (same dir).
- F6/F7 (dev_tools-gated hotkeys): see memory
  `project_dev_tools_gate.md` for the gating pattern.

### NSEvent pipeline (A.3 / PR #121, 2026-05-27)

macOS получает keyboard/mouse/scroll/modifier flags через NSEvent
local monitor, не через `SDL_PollEvent`. Controller (gamepad) и
window events продолжают идти через SDL.

- **Static keyCode → SDL_Scancode таблица**: anonymous namespace в
  `src/xrEngine/xr_input.cpp` под `#if XR_PLATFORM_APPLE`. Apple
  HID `kVK_*` константы inline-defined (Carbon header не resolve'ится
  в `.cpp` TU — см. «Apple gotchas» ниже). Включает `kVK_ISO_Section`
  (0x0A) → SCANCODE_GRAVE для console-open binding на ISO European
  keyboards (Mac RU/EU layouts).
- **Local monitor handler**: `OpenXRay_InstallNSEventMonitor()` в
  `src/xrEngine/macos_cocoa_shim.mm`. Consume'ит keyboard/flags
  (return nil); mouse/scroll pushaет в ring queue [256] и тоже
  consume'ит.
- **Drain hook**: `CInput::NSEventDrain()` в `OnFrame()`. Транслирует
  records в существующие `IR_OnKeyboard*` / `IR_OnMouse*` callbacks.
  **ВАЖНО**: drain также пишет `keyboardState[sc]` / `mouseState[btn]` —
  engine's hold loop (KeyUpdate:689 / MouseUpdate) читает state,
  не events. Без записи hold-based actions (WASD движение, sprint)
  не активируются.
- **Cvar runtime rollback**: `nsevent_input 0/1` (default 1) в
  `src/Layers/xrRender/xrRender_console.cpp`. Setter callback дёргает
  `OpenXRay_SyntheticReleaseAllKeys` → `CInput::IR_ReleaseAll()` для
  soft-reset state между pipeline'ами.
- **Stuck-key recovery**: `applicationWillResignActive:` (focus loss)
  и `workspaceWillSleep:` (system sleep) → `OpenXRay_SyntheticReleaseAllKeys`.
  `applicationDidBecomeActive:` дополнительно sync'ит
  `g_lastShimModifierFlags` через `OpenXRay_SyncModifierFlags`.
- **Capture-mode bridge**: `GrabInput()` в xr_input.cpp дублирует
  `SDL_SetRelativeMouseMode` вызов через `OpenXRay_SetMouseCaptureMode`
  чтобы shim знал captured/non-captured для mouse coord translation
  (deltas vs absolute × backingScaleFactor).
- **Backing scale tracking**: `windowDidChangeBackingProperties:`
  observer обновляет `g_backingScaleFactor` при переезде окна между
  мониторами 1x/2x.
- **Dev verify command**: `verify_input_table` (gated `dev_tools 1`)
  walks таблицу, сверяет round-trip через `SDL_GetKeyFromScancode`.

### Apple gotchas (input slice)

- **Carbon HIToolbox headers**: `<Carbon/HIToolbox/Events.h>` НЕ
  resolve'ится из `.cpp` TU — sub-framework include syntax работает
  только в `.m`/`.mm`. Solution: inline-define `kVK_*` константы
  под `#if XR_PLATFORM_APPLE` (frozen с Apple Extended Keyboard II
  ~1990). Применяется в xr_input.cpp:21-126.
- **NSEvent FlagsChanged ≠ KeyDown/KeyUp**: модификаторы (Shift,
  Ctrl, Option, Command, CapsLock) приходят как FlagsChanged events
  без явного down/up. Drain должен diff'ить `modifierFlags` против
  предыдущего snapshot'а и эмитить press/release per-flag-bit. См.
  `CInput::NSEventDrain` FlagsChanged case + `ModifierKeyCodeToScancode`
  helper.
- **NSEvent OtherMouse не различает MOUSE_4/5 (X1/X2)**: все
  «other» buttons → `mouseButton=2` = MOUSE_3. SDL различает через
  `event.button.button`. Под `nsevent_input=1` сайдовые mouse кнопки
  недоступны. Default CoP binds их не используют — non-blocker; fix
  через `[event buttonNumber]` если потребуется.
- **ISO European keyboards**: клавиша §/±/ёЁ между левым Shift и Z/Я
  имеет `kVK_ISO_Section` (0x0A), не `kVK_ANSI_Grave` (0x32). SDL
  свопал их runtime для совместимости с US ANSI bindings. Мы маппим
  обе на SCANCODE_GRAVE (без runtime detection) — обе работают как
  console-open, безвредно.

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
- Hang debugging: [`notes/decisions/hang-taxonomy.md`](../decisions/hang-taxonomy.md)
  делит hangs на две root-cause family — Family 1 (main-thread
  cooperative-contract violation; surfaces 1.A blocking I/O, 1.C
  GPU/IOKit hold) и Family 2 (static destruction ordering; surface
  2.B destructor cascade). Consult перед debugging любого нового hang
  report — не изобретай таксономию заново.
- Window / fullscreen lifecycle: `src/xrEngine/Device_mode.cpp:109`
  (`UpdateWindowProps`) — четыре пути по `psDeviceMode.WindowStyle`:
  rsWindowed / rsWindowedBorderless (`SDL_DISABLE`), rsFullscreenBorderless
  (`SDL_WINDOW_FULLSCREEN_DESKTOP`, line 144), rsFullscreen
  (`SDL_WINDOW_FULLSCREEN`, line 149 — Apple-gated → DESKTOP, см. #97).
  `SDL_GL_GetDrawableSize` сразу после set (HiDPI pixel resize).
  Mouse grab: `src/xrEngine/xr_input.cpp:643` (`GrabInput`) — тогглится
  на focus loss/gain, console/menu open. Title-bar hit-test:
  `Device_Initialize.cpp:80` регистрирует `WindowHitTest`
  (`Device_mode.cpp:124-177`). Двойной `UpdateWindowProps` в
  `Device_destroy.cpp:51,56` — band-aid timing workaround, по подозрению
  на RT-resize race (см. #48 follow-up).

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
