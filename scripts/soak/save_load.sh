#!/usr/bin/env bash
# Save/load soak harness for OpenXRay on macOS.
#
# Drives N quicksave/quickload cycles via AppleScript key sends to the
# game window, then summarises whether anything went bad in the engine
# log (R_ASSERT, ALife format mismatch, 0x502 burst, crash). Cycles =
# F6 (quicksave) → wait → F9 (quickload) → wait → repeat.
#
# Tunable via env:
#   SOAK_CYCLES        — how many save+load round-trips (default 100)
#   SOAK_SAVE_DELAY    — seconds to wait after F6 before F9 (default 4)
#   SOAK_LOAD_DELAY    — seconds to wait after F9 before next F6 (default 12)
#   SOAK_LOG           — engine log path (default openxray.log)
#
# Usage:
#   1. Launch OpenXRay-Dev.app, start a known game state, leave at idle.
#   2. Run this script from another Terminal. It will:
#       a. Note current log line count.
#       b. Bring the game window forward.
#       c. Send F6 / F9 cycles with the configured delays.
#       d. Print a summary of any error lines that appeared since start.
#
# DO NOT TOUCH KEYBOARD/MOUSE while soak runs — AppleScript needs the
# game to stay in focus to receive the key events.

set -euo pipefail

CYCLES="${SOAK_CYCLES:-100}"
SAVE_DELAY="${SOAK_SAVE_DELAY:-4}"
LOAD_DELAY="${SOAK_LOAD_DELAY:-12}"
LOG="${SOAK_LOG:-$HOME/Library/Logs/OpenXRay/openxray.log}"
APP_NAME="${SOAK_APP_NAME:-OpenXRay-Dev}"

if [[ ! -f "$LOG" ]]; then
    echo "soak: engine log not found at $LOG" >&2
    exit 1
fi

START_LINES=$(wc -l < "$LOG")
START_TS=$(date +%s)
RESULTS_DIR="${SOAK_RESULTS_DIR:-$(pwd)/_workspace/soak-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$RESULTS_DIR"
RESULTS_LOG="$RESULTS_DIR/run.log"

echo "soak: cycles=$CYCLES save_delay=${SAVE_DELAY}s load_delay=${LOAD_DELAY}s"
echo "soak: results dir=$RESULTS_DIR"
echo "soak: engine log starts at line $START_LINES"
echo "soak: focusing $APP_NAME in 3 seconds — DO NOT TOUCH INPUT"
sleep 3

# Focus the game window. The .app process name is "xr_3da" (the binary)
# but the bundle is OpenXRay-Dev.app. Try the bundle name first.
osascript -e "tell application \"${APP_NAME}\" to activate" 2>/dev/null \
    || osascript -e 'tell application "xr_3da" to activate' \
    || { echo "soak: could not focus game window" >&2; exit 1; }
sleep 1

press_key() {
    # macOS virtual keycodes: F5=96, F6=97, F9=101.
    local keycode="$1"
    osascript -e "tell application \"System Events\" to key code $keycode"
}

# Keybinds in user.ltx: bind quick_save kF5, bind quick_load kF9.
SAVE_KEY="${SOAK_SAVE_KEY:-96}"   # F5 by default — match user.ltx
LOAD_KEY="${SOAK_LOAD_KEY:-101}"  # F9 by default — match user.ltx

for ((i = 1; i <= CYCLES; i++)); do
    cycle_start=$(date +%s)

    echo "soak: cycle $i/$CYCLES — save (key $SAVE_KEY)" | tee -a "$RESULTS_LOG"
    press_key "$SAVE_KEY"
    sleep "$SAVE_DELAY"

    echo "soak: cycle $i/$CYCLES — load (key $LOAD_KEY)" | tee -a "$RESULTS_LOG"
    press_key "$LOAD_KEY"
    sleep "$LOAD_DELAY"

    cycle_end=$(date +%s)
    echo "soak: cycle $i took $((cycle_end - cycle_start))s" >> "$RESULTS_LOG"
done

END_TS=$(date +%s)
END_LINES=$(wc -l < "$LOG")

# Extract the log slice for this run.
tail -n "+$((START_LINES + 1))" "$LOG" > "$RESULTS_DIR/engine-slice.log"

# Summary counts.
{
    echo "==== soak summary ===="
    echo "cycles_run:        $CYCLES"
    echo "wall_seconds:      $((END_TS - START_TS))"
    echo "log_lines_added:   $((END_LINES - START_LINES))"
    echo
    echo "==== error counts (in slice) ===="
    # grep -c always prints the count; the previous `|| echo 0` fallback
    # caused double-zero output when there were no matches (grep prints
    # "0" then exits 1, fallback echoed another "0"). Use `|| true`.
    for pat in 'R_ASSERT' '! ALife save format' 'shader compilation failed' \
               '0x502' 'OpenGL: 0x' 'FATAL' 'crash' 'segmentation fault'; do
        c=$(grep -c "$pat" "$RESULTS_DIR/engine-slice.log" || true)
        printf '  %-32s %s\n' "$pat" "${c:-0}"
    done
} | tee "$RESULTS_DIR/summary.txt"

echo
echo "soak: full slice at $RESULTS_DIR/engine-slice.log"
echo "soak: summary at    $RESULTS_DIR/summary.txt"
