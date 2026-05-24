#!/usr/bin/env bash
# scripts/end-of-session.sh — interactive end-of-session ritual
#
# Walks the 5 EoS ritual steps from notes/conventions/working-agreement.md
# and prompts y/n/skip on each. Records which ones still need attention.
#
# Why: the ritual is documented but relies on Claude remembering at
# session end. Memory-by-discipline fails when context is heavy.
# Script makes the prompt unavoidable (memory feedback_scriptify_repeats).
#
# Usage:
#   ./scripts/end-of-session.sh
#
# This is foreground Claude's tool — invoke it in a Bash call near the
# end of a significant session. Output goes to stdout for review;
# pending items show up at the bottom.

set -u

REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null)" || {
    echo "[eos] FAIL: not in a git repo"
    exit 2
}
cd "$REPO_ROOT"

MEMORY_DIR="/Users/ragnar/.claude/projects/-Users-ragnar-fedorov-tech-xray-16/memory"

echo "==============================================="
echo "  End-of-session ritual (5 steps)"
echo "  Source: notes/conventions/working-agreement.md"
echo "==============================================="
echo

pending=()

ask() {
    local n="$1"
    local prompt="$2"
    local hint="$3"
    echo "[$n/5] $prompt"
    echo "      hint: $hint"
    while true; do
        printf "      done? [y/n/s=skip] "
        read -r ans
        case "$ans" in
            y|Y) echo "      ✓ marked done"; echo; return 0 ;;
            n|N) pending+=("[$n] $prompt"); echo "      ↻ added to pending list"; echo; return 0 ;;
            s|S) echo "      → skipped"; echo; return 0 ;;
            *)   echo "      (please answer y, n, or s)" ;;
        esac
    done
}

ask 1 \
    "MEMORY.md: any durable feedback to persist?" \
    "If ragnar said something quotable about how to work — capture in $MEMORY_DIR/feedback_<name>.md + add line to MEMORY.md"

ask 2 \
    "notes/conventions/working-agreement.md: new rule emerge?" \
    "If a process pattern crystallized this session — add a section + commit"

ask 3 \
    "notes/playbooks/gotchas.md: new landmine surfaced?" \
    "If a non-obvious bug class bit us — write it up before forgetting"

ask 4 \
    "notes/reference/engine-map.md: new code touchpoint worth indexing?" \
    "Any file:line worth keeping for next session's lookup — fold it in"

ask 5 \
    "Parked decisions: explicit OR closed?" \
    "Any 'we'll decide later' must have a file + open question recorded, not loose"

echo "==============================================="
if [ ${#pending[@]} -eq 0 ]; then
    echo "  All ritual steps clear. Session can close cleanly."
    echo "==============================================="
    exit 0
fi

echo "  Pending items before close:"
for p in "${pending[@]}"; do
    echo "    - $p"
done
echo "==============================================="
echo "  Address these before the session ends, or document why parked."
exit 1
