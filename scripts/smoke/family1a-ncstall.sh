#!/usr/bin/env bash
# Family 1.A reproducer: stalled TCP listener vs ghttp polling.
#
# Pre A.2: engine main loop wedged on ghttp's synchronous DNS / select chain
# when manifest URL points at a black-hole endpoint. After A.2 (commit
# 9f2232d21 + the teardown safety commit on top), ghttp polling runs on a
# serial dispatch_queue worker; main loop is unaffected.
#
# Usage:
#   1. Run this script in one terminal. nc listens on 127.0.0.1:8000 forever.
#   2. Launch the engine in another terminal.
#   3. In the engine console: updater_manifest_url http://127.0.0.1:8000/manifest.json
#   4. Trigger an update check (main-menu "Check for updates" button or
#      the equivalent console command).
#
# Pass criteria:
#   * ps -M -p $(pgrep -f xr_3da) shows main thread STAT=S, never U/D.
#   * FPS HUD keeps ticking for >60s while nc-stall holds.
#   * Cmd+Q completes in <1s.
#   * Force Quit (Cmd+Opt+Esc) completes within 10s watchdog budget
#     (macos_cocoa_shim.mm:108-114).
#   * Sample-hang (`make sample-hang`) shows worker thread inside
#     OpenXRay_GhttpDispatchAsync block, main thread NOT in ghttp* frames.
#
# Known limitation: if DNS resolves slowly AND Cmd+Q fires simultaneously,
# the dispatch_sync(ghttpCleanup) barrier in CGameSpy_HTTP::CleanUp can hold
# main for up to libc's gethostbyname timeout (~30s on Darwin). The 10s
# watchdog SIGKILL is the fallback. See A.2.1 follow-up for proper submodule
# patch (getaddrinfo swap).

set -euo pipefail

PORT="${1:-8000}"
HOST="${HOST:-127.0.0.1}"

echo "Starting nc-stall on ${HOST}:${PORT} (Ctrl-C to stop)..."
nc -l "${HOST}" "${PORT}"
