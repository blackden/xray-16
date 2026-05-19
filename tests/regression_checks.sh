#!/usr/bin/env bash
# Regression checks for engine fixes made in 2026-05 macOS bring-up.
#
# Static grep-based assertions that pin the SHAPE of the fix on disk. Cheap;
# doesn't validate runtime behavior. Pair with safe_append_test for the parts
# we can isolate from xrCore's heavy init.

set -u
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

fail=0
pass=0

check() {
    local name="$1"
    local cmd="$2"
    if eval "$cmd" >/dev/null 2>&1; then
        printf '  \033[32m✓\033[0m %s\n' "$name"
        pass=$((pass + 1))
    else
        printf '  \033[31m✗\033[0m %s\n' "$name"
        printf '      command: %s\n' "$cmd"
        fail=$((fail + 1))
    fi
}

echo "== xrCore::LocatorAPI fixes =="

# rescan_path: early return was removed. If someone reintroduces
# 'if (I == m_files.end()) return;' inside rescan_path, the overlay (-overlaypath)
# stops being indexed and FS.exist returns false for files under it.
check "rescan_path no longer early-returns on lower_bound == end()" \
    "! awk '/^void CLocatorAPI::rescan_path/,/^void CLocatorAPI::rescan_pathes/' \
        src/xrCore/LocatorAPI.cpp | \
        grep -E 'if \\(I == m_files\\.end\\(\\)\\)[[:space:]]*$|return;' | \
        grep -B1 '^[[:space:]]*return;[[:space:]]*$' | \
        grep -q 'I == m_files.end()'"

check "rescan_path still calls Recurse(full_path)" \
    "awk '/^void CLocatorAPI::rescan_path/,/^void CLocatorAPI::rescan_pathes/' \
        src/xrCore/LocatorAPI.cpp | grep -q 'Recurse(full_path)'"

# _set_root: must normalize POSIX-style separators to engine internal '\\'.
# Without this, m_Path under -overlaypath stays mixed-slash while indexed
# entries are all-backslash -- m_files.find never matches and FS.exist lies.
check "FS_Path::_set_root normalizes separators" \
    "awk '/^void FS_Path::_set_root/,/^}/' src/xrCore/LocatorAPI_defs.cpp | \
        grep -q 'restore_path_separators(temp)'"

echo
echo "== xrDebug fixes =="

# GatherInfo: must use the safe_append helper, not the raw
# 'buffer += xr_sprintf(buffer, end - buffer, ...)' pattern that overflows
# string4096 on long stack traces and PAC-traps on ARM64 macOS.
check "xrDebug.cpp defines safe_append helper" \
    "grep -q 'void safe_append(char\\*& buffer' src/xrCore/xrDebug.cpp"

check "xrDebug::GatherInfo uses safe_append" \
    "awk '/^void xrDebug::GatherInfo/,/^void xrDebug::Fatal/' src/xrCore/xrDebug.cpp | \
        grep -q 'safe_append('"

check "xrDebug.cpp no longer uses 'buffer += xr_sprintf' pattern in GatherInfo" \
    "! awk '/^void xrDebug::GatherInfo/,/^void xrDebug::Fatal/' src/xrCore/xrDebug.cpp | \
        grep -q 'buffer += xr_sprintf'"

check "xrDebug::ErrorToString returns non-null on non-Windows" \
    "awk '/^pcstr xrDebug::ErrorToString/,/^int out_of_memory_handler/' src/xrCore/xrDebug.cpp | \
        grep -q '#else' && \
     awk '/^pcstr xrDebug::ErrorToString/,/^int out_of_memory_handler/' src/xrCore/xrDebug.cpp | \
        grep -q 'result = \"'"

echo
echo "== Apple-specific fixes =="

# CHK_GL on Apple must log glGetError() even in MasterGold (non-DEBUG) builds,
# so we have a forensic trail when the GPU driver enters a stuck state.
check "CHK_GL logs glGetError on Apple in MasterGold branch" \
    "awk '/^#else \\/\\/ DEBUG/,/^#endif \\/\\/ DEBUG/' src/xrCore/xrDebug_macros.h | \
        grep -q 'XR_PLATFORM_APPLE' && \
     awk '/^#else \\/\\/ DEBUG/,/^#endif \\/\\/ DEBUG/' src/xrCore/xrDebug_macros.h | \
        grep -q 'glGetError'"

# Occlusion queries on GL must poll GL_QUERY_RESULT_AVAILABLE before blocking
# on GL_QUERY_RESULT -- otherwise the caller's existing wait loop is dead
# code and each query stalls the main thread.
check "GL occlusion GetData polls GL_QUERY_RESULT_AVAILABLE" \
    "awk '/^IC HRESULT GetData\\(GLuint query/,/^}/' src/Layers/xrRender/QueryHelper.h | \
        grep -q 'GL_QUERY_RESULT_AVAILABLE'"

echo
echo "== Apple launcher safety nets =="

# Launcher must force windowed mode on macOS so users have a real NSWindow
# (-> Cmd+Tab / Force Quit / Activity Monitor work when the engine hangs).
check "launcher forces vid_window_mode in user.ltx" \
    "grep -q 'vid_window_mode st_opt_windowed' scripts/mac/package_app.sh"

check "launcher forces rs_fullscreen off" \
    "grep -q 'rs_fullscreen off' scripts/mac/package_app.sh"

echo
echo "== UTF-8 migration foundation =="

# The cp1251 <-> UTF-8 helpers are the bridge that every later migration phase
# leans on. If anyone removes them by accident the autosave EILSEQ retry breaks
# and the renderer migration loses its boundary normaliser.
check "xrCore exports xr_cp1251_to_utf8" \
    "grep -q 'XRCORE_API void xr_cp1251_to_utf8' src/xrCore/xrCore.h"

check "xrCore exports xr_utf8_to_cp1251" \
    "grep -q 'XRCORE_API void xr_utf8_to_cp1251' src/xrCore/xrCore.h"

check "xrCore exports xr_is_valid_utf8 (RFC 3629 validator)" \
    "grep -q 'XRCORE_API bool xr_is_valid_utf8' src/xrCore/xrCore.h"

check "xr_is_valid_utf8 has surrogate guard (ED A0..)" \
    "awk '/XRCORE_API bool xr_is_valid_utf8/,/^}/' src/xrCore/xrCore.cpp | grep -q '0xED' "

check "xr_is_valid_utf8 has overlong guard (C2 minimum for 2-byte)" \
    "awk '/XRCORE_API bool xr_is_valid_utf8/,/^}/' src/xrCore/xrCore.cpp | grep -q '0xC2'"

# EILSEQ retry now lives in xrCore: CFileWriter on write, LocatorAPI on read.
# alife_storage_manager intentionally has no transcoding code — if anyone
# resurrects it there, both layers run the conversion and the per-callsite
# normalization is dead code.
check "CFileWriter retries fopen on EILSEQ after cp1251->utf8 transcode" \
    "awk '/class CFileWriter/,/^};/' src/xrCore/FS_internal.h | \
        grep -q 'retry_fopen_after_cp1251'"

check "CLocatorAPI::check_for_file retries lookup with utf8-transcoded name" \
    "grep -A 50 'CLocatorAPI::check_for_file' src/xrCore/LocatorAPI.cpp | \
        grep -q 'xr_cp1251_to_utf8'"

check "alife_storage_manager has no transcoding (handled one layer down)" \
    "! grep -q 'xr_cp1251_to_utf8' src/xrGame/alife_storage_manager.cpp"

# Phase 2 read shims: XML and INI loaders must auto-transcode legacy
# cp1251/cp1250 bodies to UTF-8 on read, otherwise menu / dialog text
# regresses to '?' after the Phase 1 codepoint renderer flip.
check "XMLDocument::Load uses SetWithEncodingShim" \
    "awk '/XMLDocument::Load.*xml_filename, bool fatal/,/^bool XMLDocument::Set/' \
        src/xrCore/XML/XMLDocument.cpp | grep -q 'SetWithEncodingShim'"

check "XMLDocument::SetWithEncodingShim transcodes via xr_legacy_to_utf8_alloc" \
    "grep -q 'xr_legacy_to_utf8_alloc' src/xrCore/XML/XMLDocument.cpp"

check "CInifile::Load auto-transcodes cp1251 lines" \
    "awk '/^void CInifile::Load/,/^void CInifile::save_as/' src/xrCore/xr_ini.cpp | \
        grep -q 'xr_cp1251_to_utf8'"

# Save-side compatibility shim: CSE_ALifeTraderAbstract::m_character_name
# and CInventoryOwner::m_game_name are serialized strings; legacy cp1251
# saves must be transcoded on load or NPC labels show '???'.
check "CSE_ALifeTraderAbstract::STATE_Read transcodes m_character_name" \
    "awk '/CSE_ALifeTraderAbstract::STATE_Read/,/^void CSE_ALifeTraderAbstract::OnChangeProfile/' \
        src/xrServerEntities/xrServer_Objects_ALife_Monsters.cpp | grep -q 'xr_cp1251_to_utf8'"

check "CInventoryOwner::load transcodes m_game_name" \
    "awk '/CInventoryOwner::load/,/^void CInventoryOwner::UpdateInventoryOwner/' \
        src/xrGame/InventoryOwner.cpp | grep -q 'xr_cp1251_to_utf8'"

# Key bindings: use SDL_GetScancodeName (layout-independent) so prompts
# stay Latin even on RU keyboards, while text input keeps respecting the
# active layout.
check "KbdKeyToButtonName uses SDL_GetScancodeName" \
    "awk '/^bool KbdKeyToButtonName/,/^bool OtherDevicesKeyToButtonName/' \
        src/xrEngine/xr_input.cpp | grep -q 'SDL_GetScancodeName'"

# Phase 4: localization XMLs are UTF-8 on disk + CMake validator catches drift.
check "localization XMLs declare encoding=utf-8" \
    "for f in res/gamedata/configs/text/*/openxray.xml; do \
        head -1 \"\$f\" | grep -q 'encoding=\"utf-8\"' || exit 1; \
     done"

check "localization XMLs are valid UTF-8 on disk" \
    "for f in res/gamedata/configs/text/*/openxray.xml; do \
        iconv -f UTF-8 -t UTF-8 < \"\$f\" >/dev/null 2>&1 || exit 1; \
     done"

check "res/CMakeLists.txt registers validate-localization-utf8 target" \
    "grep -q 'validate-localization-utf8' res/CMakeLists.txt"

# Fixtures should be present and well-formed. Stale or missing fixtures will
# cause the test-encoding target to fail before anyone reaches the C++ tests.
check "encoding fixture phrase.utf8 exists" \
    "test -s tests/fixtures/encoding/phrase.utf8"

check "encoding fixture phrase.cp1251 exists" \
    "test -s tests/fixtures/encoding/phrase.cp1251"

check "encoding fixture phrase.cp1250 exists" \
    "test -s tests/fixtures/encoding/phrase.cp1250"

echo
echo "== Apple GL renderer state cache =="

# 0x502 storm root cause: set_Format binds a new VAO when decl changes, but
# the new VAO has no glVertexBuffer / glBindVertexBuffer binding -- yet the
# CBackend cache remembered vb/vb_stride from the previous VAO and so
# set_Vertices early-returned, skipping the bind. Result: glDrawElements on
# a VAO with no vertex source, every draw call. The fix MUST reset vb and
# vb_stride alongside ib whenever a new VAO becomes current.
check "CBackend::set_Format resets vb when decl changes (0x502 fix)" \
    "awk '/^ICF void CBackend::set_Format/,/^ICF void CBackend::set_Vertices/' \
        src/Layers/xrRenderGL/glR_Backend_Runtime.h | grep -q 'vb = 0'"

check "CBackend::set_Format resets vb_stride when decl changes" \
    "awk '/^ICF void CBackend::set_Format/,/^ICF void CBackend::set_Vertices/' \
        src/Layers/xrRenderGL/glR_Backend_Runtime.h | grep -q 'vb_stride = 0'"

# Apple Cmd+Q in-level TX-state fix: CRenderDevice::Destroy must drain GPU
# queue before glDelete* cascade, otherwise the Metal-backed GL driver
# serializes deletes against in-flight shadow / occlusion / streaming work
# via mach_msg and the user sees a multi-second hang.
check "CRenderDevice::Destroy drains GPU queue before OnDeviceDestroy" \
    "awk '/^void CRenderDevice::Destroy/,/^void CRenderDevice::Reset/' \
        src/xrEngine/Device_destroy.cpp | grep -q 'FlushGpuQueue'"

# GL draw path retains 32-line storm cap so a future regression that
# reintroduces a per-frame error doesn't fill disk.
check "GL draw error log capped at 32 messages per storm" \
    "awk '/Phase 5 0x502 hunt/,/^#else$/' src/Layers/xrRenderGL/glR_Backend_Runtime.h | \
        grep -q 'reported < 32'"

echo
echo "== Safe-mode sentinel =="

# Engine paths use Windows-style backslashes; POSIX stat()/unlink() reject
# them. The sentinel cleanup MUST normalize separators before calling
# stat() or it silently fails to find the file even when present.
check "x_ray.cpp sentinel cleanup calls convert_path_separators" \
    "awk '/Safe-mode boot recovery/,/sentinelCleared = true/' src/xrEngine/x_ray.cpp | \
        grep -q 'convert_path_separators(sentinel)'"

check "x_ray.cpp clears sentinel after 120 stable frames" \
    "grep -q 'STABLE_BOOT_FRAMES = 120' src/xrEngine/x_ray.cpp"

# Launcher writes sentinel into _appdata_/ because LocatorAPI resolves
# \$app_data_root\$ with a _appdata_/ prefix; mismatch broke safe-mode in
# 1b85d75ef and was relanded in 031c95747.
check "launcher sentinel path includes _appdata_/ prefix" \
    "grep -q '_appdata_/\\.boot_in_progress' scripts/mac/package_app.sh"

echo
echo "== Log housekeeping =="

# Existing log over 100MB at boot is almost always an error-storm tail.
# Keeping it as .bkp doubles disk usage on the next crash cycle, so the
# rotation path must unlink instead of rename in that case.
check "CreateLog caps existing log at 100MB (unlink instead of .bkp)" \
    "awk '/void CreateLog/,/^void CloseLog/' src/xrCore/log.cpp | \
        grep -q 'LOG_ROTATE_LIMIT_BYTES'"

echo
echo "== Renderer playground (v0) =="

# v0 playground exists and registers an action / hotkey path. The ide_tool
# pattern is the foundation; the hotkey is the only entry point under
# MasterGold (#ifndef MASTER_GOLD gates the Tools menu).
check "RendererPlayground.h exists" \
    "test -f src/xrEngine/RendererPlayground.h"

check "RendererPlayground.cpp exists" \
    "test -f src/xrEngine/RendererPlayground.cpp"

check "kRENDER_PLAYGROUND action declared in xr_level_controller.h" \
    "grep -q 'kRENDER_PLAYGROUND' src/xrEngine/xr_level_controller.h"

check "kRENDER_PLAYGROUND named in xr_level_controller.cpp" \
    "grep -q '\"render_playground\"' src/xrEngine/xr_level_controller.cpp"

check "kRENDER_PLAYGROUND default-bound to F11 (developer-only; macOS disables Mission Control side)" \
    "awk '/kRENDER_PLAYGROUND,/' src/xrEngine/xr_level_controller.cpp | grep -q 'SDL_SCANCODE_F11'"

check "ide::IR_OnKeyboardPress routes kRENDER_PLAYGROUND" \
    "awk '/^void ide::IR_OnKeyboardPress/,/^void ide::IR_OnKeyboardRelease/' src/xrEngine/editor_base_input.cpp | \
        grep -q 'case kRENDER_PLAYGROUND'"

# DummyReceiver is the always-captured fallback; without a case here the
# playground hotkey only works when the console is open (i.e. when ide
# happens to be the active input receiver).
check "DummyReceiver routes kRENDER_PLAYGROUND (fallback path)" \
    "awk '/^class DummyReceiver/,/^} dummyController/' src/xrEngine/xr_input.cpp | \
        grep -q 'case kRENDER_PLAYGROUND'"

# Input dispatch is top-of-stack only (cbStack.back()->IR_OnKeyboardPress).
# kRENDER_PLAYGROUND must be routed by every active top-of-stack receiver
# the user can be sitting in: gameplay (CLevel), main menu (CMainMenu),
# console (CConsole - delegates to ide), full editor (ide directly).
check "CLevel::IR_OnKeyboardPress routes kRENDER_PLAYGROUND (gameplay)" \
    "awk '/^void CLevel::IR_OnKeyboardPress/,/^void CLevel::IR_OnKeyboardRelease/' src/xrGame/Level_input.cpp | \
        grep -q 'kRENDER_PLAYGROUND'"

check "CMainMenu::IR_OnKeyboardPress routes kRENDER_PLAYGROUND (main menu)" \
    "awk '/^void CMainMenu::IR_OnKeyboardPress/,/^void CMainMenu::IR_OnKeyboardRelease/' src/xrGame/MainMenu.cpp | \
        grep -q 'kRENDER_PLAYGROUND'"

check "ide exports TogglePlayground (shared toggle path between DummyReceiver and ide receiver)" \
    "grep -q 'void TogglePlayground' src/xrEngine/editor_base.h"

check "RendererPlayground constructed in ide::InitBackend" \
    "awk '/^void ide::InitBackend/,/^void ide::ProcessEvent/' src/xrEngine/editor_base_input.cpp | \
        grep -q 'make_unique<RendererPlayground>'"

# v1: GL state + RT picker
check "IRender exposes PlaygroundGLState query" \
    "grep -q 'GetPlaygroundGLState' src/xrEngine/Render.h"

check "IRender exposes EnumerateRenderTargets query" \
    "grep -q 'EnumerateRenderTargets' src/xrEngine/Render.h"

check "D3DXRenderBase implements GetPlaygroundGLState" \
    "awk '/D3DXRenderBase::GetPlaygroundGLState/,/^}/' src/Layers/xrRender/D3DXRenderBase.cpp | \
        grep -q 'glGetIntegerv'"

check "CResourceManager exposes ForEachRT for read-only RT iteration" \
    "grep -q 'void ForEachRT' src/Layers/xrRender/ResourceManager.h"

check "RendererPlayground has GL State tab" \
    "grep -q 'DrawGLStateTab' src/xrEngine/RendererPlayground.cpp"

check "RendererPlayground has RT Picker tab" \
    "grep -q 'DrawRTPickerTab' src/xrEngine/RendererPlayground.cpp"

# CHK_GL ring buffer for the Event Log tab
check "xrCore declares xr_gl_error_sink function pointer" \
    "grep -q 'xr_gl_error_sink_fn' src/xrCore/xrDebug_macros.h"

check "xrCore defines xr_gl_error_sink (default nullptr)" \
    "grep -q 'xr_gl_error_sink = nullptr' src/xrCore/xrDebug.cpp"

check "Apple CHK_GL (Debug branch) calls xr_gl_error_sink" \
    "awk '/^#if defined\\(XR_PLATFORM_APPLE\\)/,/^#else$/' src/xrCore/xrDebug_macros.h | \
        head -25 | grep -q 'xr_gl_error_sink('"

check "Apple CHK_GL (MasterGold branch) calls xr_gl_error_sink" \
    "grep -c 'xr_gl_error_sink(' src/xrCore/xrDebug_macros.h | awk '{exit !(\$1 >= 2)}'"

check "RendererPlayground exposes g_glErrorRing" \
    "grep -q 'extern GLErrorRing g_glErrorRing' src/xrEngine/RendererPlayground.h"

check "RendererPlayground wires xr_gl_error_sink in InitBackend" \
    "awk '/^void ide::InitBackend/,/^void ide::ProcessEvent/' src/xrEngine/editor_base_input.cpp | \
        grep -q 'xr_gl_error_sink ='"

check "RendererPlayground has Event Log tab" \
    "grep -q 'DrawEventLogTab' src/xrEngine/RendererPlayground.cpp"

# v2: Pipeline stage toggles
check "IRender declares DebugRenderToggles struct" \
    "grep -q 'struct DebugRenderToggles' src/xrEngine/Render.h"

check "D3DXRenderBase exposes m_debugToggles member" \
    "grep -q 'DebugRenderToggles m_debugToggles' src/Layers/xrRender/D3DXRenderBase.h"

check "r2_R_render.cpp gates sun cascades on m_debugToggles.shadows" \
    "awk '/Directional light/,/accum_direct_blend/' src/Layers/xrRender_R2/r2_R_render.cpp | \
        grep -q 'm_debugToggles.shadows'"

check "r2_R_render.cpp gates phase_occq on m_debugToggles.occq" \
    "grep -q 'm_debugToggles.occq.*phase_occq\\|phase_occq.*m_debugToggles.occq' src/Layers/xrRender_R2/r2_R_render.cpp || \
     awk '/Occlusion testing/,/phase_occq/' src/Layers/xrRender_R2/r2_R_render.cpp | grep -q 'm_debugToggles.occq'"

check "r2_R_render.cpp gates Details->Render on m_debugToggles.details" \
    "grep -c 'm_debugToggles.details' src/Layers/xrRender_R2/r2_R_render.cpp | awk '{exit !(\$1 >= 2)}'"

check "r2_R_render.cpp gates wallmarks on m_debugToggles.wallmarks" \
    "grep -q 'Wallmarks.*m_debugToggles.wallmarks\\|m_debugToggles.wallmarks.*Wallmarks' src/Layers/xrRender_R2/r2_R_render.cpp"

check "RendererPlayground has Pipeline Toggles tab" \
    "grep -q 'DrawPipelineTogglesTab' src/xrEngine/RendererPlayground.cpp"

# v2: Shader hot-reload watcher (Apple FSEvents)
check "RendererPlayground_HotReload.mm exists" \
    "test -f src/xrEngine/RendererPlayground_HotReload.mm"

check "Hot-reload uses FSEvents framework" \
    "grep -q 'FSEventStreamCreate' src/xrEngine/RendererPlayground_HotReload.mm"

check "CMakeLists links CoreServices for FSEvents" \
    "grep -q 'CoreServices' src/xrEngine/CMakeLists.txt"

check "CMakeLists builds RendererPlayground_HotReload.mm with -fobjc-arc" \
    "awk '/RendererPlayground_HotReload.mm/,/COMPILE_FLAGS/' src/xrEngine/CMakeLists.txt | \
        grep -q 'fobjc-arc'"

check "RendererPlayground has Hot Reload tab (Apple-only)" \
    "grep -q 'DrawHotReloadTab' src/xrEngine/RendererPlayground.cpp"

# DebugRenderToggles extended (lights toggle, post-epic addition)
check "DebugRenderToggles includes lights gate" \
    "awk '/struct DebugRenderToggles/{found=1; next} found{if(/^[[:space:]]*\\};/){exit} else print}' \
        src/xrEngine/Render.h | grep -q 'bool lights'"

check "r2_R_render.cpp gates render_lights on m_debugToggles.lights" \
    "grep -c 'm_debugToggles.lights' src/Layers/xrRender_R2/r2_R_render.cpp | awk '{exit !(\$1 >= 2)}'"

# ide framework second consumer — ALife Inspector
check "ALifeInspector.h exists" \
    "test -f src/xrGame/ALifeInspector.h"

check "ALifeInspector.cpp exists" \
    "test -f src/xrGame/ALifeInspector.cpp"

check "kALIFE_INSPECTOR action declared" \
    "grep -q 'kALIFE_INSPECTOR' src/xrEngine/xr_level_controller.h"

check "kALIFE_INSPECTOR default-bound to F12 (gated by dev_tools cvar)" \
    "awk '/kALIFE_INSPECTOR,.*SDL_SCANCODE/' src/xrEngine/xr_level_controller.cpp | grep -q 'SDL_SCANCODE_F12'"

check "g_dev_tools defined with MASTER_GOLD-aware default in xr_ioc_cmd.cpp" \
    "awk '/g_dev_tools/' src/xrEngine/xr_ioc_cmd.cpp | grep -q 'ENGINE_API int g_dev_tools'"

check "g_dev_tools default 0 under MASTER_GOLD" \
    "awk '/MASTER_GOLD/,/endif/' src/xrEngine/xr_ioc_cmd.cpp | grep -q 'g_dev_tools = 0'"

check "dev_tools cvar registered with CCC_Integer" \
    "grep -q 'CCC_Integer, \"dev_tools\"' src/xrEngine/xr_ioc_cmd.cpp"

check "g_dev_tools extern declared in xr_level_controller.h" \
    "grep -q 'extern ENGINE_API int g_dev_tools' src/xrEngine/xr_level_controller.h"

check "ide::IR_OnKeyboardPress gates kRENDER_PLAYGROUND on g_dev_tools" \
    "awk '/case kRENDER_PLAYGROUND:/,/return;/' src/xrEngine/editor_base_input.cpp | grep -q 'g_dev_tools'"

check "ide::IR_OnKeyboardPress gates kALIFE_INSPECTOR on g_dev_tools" \
    "awk '/case kALIFE_INSPECTOR:/,/return;/' src/xrEngine/editor_base_input.cpp | grep -q 'g_dev_tools'"

check "CLevel::IR_OnKeyboardPress gates kRENDER_PLAYGROUND on g_dev_tools" \
    "awk '/_curr == kRENDER_PLAYGROUND/,/^    }/' src/xrGame/Level_input.cpp | grep -q 'g_dev_tools'"

check "CLevel::IR_OnKeyboardPress gates kALIFE_INSPECTOR on g_dev_tools" \
    "awk '/_curr == kALIFE_INSPECTOR/,/^    }/' src/xrGame/Level_input.cpp | grep -q 'g_dev_tools'"

check "CMainMenu::IR_OnKeyboardPress gates kRENDER_PLAYGROUND on g_dev_tools" \
    "awk '/case kRENDER_PLAYGROUND:/,/return;/' src/xrGame/MainMenu.cpp | grep -q 'g_dev_tools'"

check "CMainMenu::IR_OnKeyboardPress gates kALIFE_INSPECTOR on g_dev_tools" \
    "awk '/case kALIFE_INSPECTOR:/,/return;/' src/xrGame/MainMenu.cpp | grep -q 'g_dev_tools'"

check "ide exposes ToggleNamedTool dispatch" \
    "grep -q 'bool ToggleNamedTool' src/xrEngine/editor_base.h"

check "xrGameModule constructs ALifeInspector in initialize" \
    "awk '/xrGameModule::initialize/,/^void xrGameModule::finalize/' src/xrGame/xrGame.cpp | \
        grep -q 'make_unique<xray::editor::ALifeInspector>'"

check "CLevel::IR_OnKeyboardPress routes kALIFE_INSPECTOR" \
    "awk '/^void CLevel::IR_OnKeyboardPress/,/^void CLevel::IR_OnKeyboardRelease/' src/xrGame/Level_input.cpp | \
        grep -q 'kALIFE_INSPECTOR'"

echo
echo "Summary: $pass passed, $fail failed"
exit $fail
