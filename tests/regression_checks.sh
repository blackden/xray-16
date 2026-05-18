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

# Fixtures should be present and well-formed. Stale or missing fixtures will
# cause the test-encoding target to fail before anyone reaches the C++ tests.
check "encoding fixture phrase.utf8 exists" \
    "test -s tests/fixtures/encoding/phrase.utf8"

check "encoding fixture phrase.cp1251 exists" \
    "test -s tests/fixtures/encoding/phrase.cp1251"

check "encoding fixture phrase.cp1250 exists" \
    "test -s tests/fixtures/encoding/phrase.cp1250"

echo
echo "Summary: $pass passed, $fail failed"
exit $fail
