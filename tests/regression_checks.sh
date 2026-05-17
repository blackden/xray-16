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
echo "Summary: $pass passed, $fail failed"
exit $fail
