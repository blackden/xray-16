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

echo
echo "Summary: $pass passed, $fail failed"
exit $fail
