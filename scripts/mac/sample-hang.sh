#!/bin/bash
# Sample a currently-hung xr_3da process and save the trace to
# ~/Downloads/sample-TIMESTAMP.txt. Use this when Cmd+Q wedges, when the
# engine hangs mid-level, or any time you want a C stack of all threads.
#
# Standard invocation (works for self-owned processes):
#   make sample-hang
#
# If macOS blocks the sample call (rare; SIP strict or hardened-runtime
# without sample entitlement), retry under sudo:
#   sudo make sample-hang
#
# Tunables:
#   SAMPLE_DURATION  seconds of sampling, default 3
#
# Output is line-buffered to stdout so the calling agent can echo the path
# back without parsing.

set -u

PID="$(pgrep -x xr_3da | head -1)"
if [ -z "$PID" ]; then
    echo "xr_3da not running" >&2
    exit 1
fi

OUT="${HOME}/Downloads/sample-$(date +%Y%m%d-%H%M%S).txt"
DURATION="${SAMPLE_DURATION:-3}"

echo "==> sampling pid=$PID for ${DURATION}s -> $OUT"
if ! sample "$PID" "$DURATION" -file "$OUT" -mayDie 2>/dev/null; then
    echo "==> sample failed (likely needs sudo). Retry: sudo make sample-hang" >&2
    exit 1
fi

echo "==> done: $OUT"
echo "==> path: $OUT"
