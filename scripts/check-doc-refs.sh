#!/usr/bin/env bash
# scripts/check-doc-refs.sh — validate notes/ doc references
#
# Walks the repo for `notes/<subtree>/<file>.md`-style references and
# checks each one points to an existing file. Exits non-zero on any
# broken refs not in scripts/doc-refs-allowlist.
#
# Why: after a notes/ reorg (#83) or any time a doc is renamed/moved,
# silently-broken refs in agent files / CLAUDE.md / code comments are
# the most common drift. Manual grep-and-eyeball each time is slow and
# unreliable (memory feedback_scriptify_repeats).
#
# Scope: only matches paths starting with `notes/`. Same-dir or
# `../sibling/` relative refs inside notes/*.md are out of scope —
# authors see those when reading the file.
#
# Usage:
#   ./scripts/check-doc-refs.sh        # standalone
#   make verify                        # combined sanity check
#   <auto>                             # pre-push hook (Check 4)
#
# Allowlist: one absolute path per line in scripts/doc-refs-allowlist.
# Use for intentional dangling refs (e.g., placeholder file names in
# decisions docs).
#
# Portable: uses BSD/GNU grep only (no rg dependency).

set -u

REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null)" || {
    echo "[check-doc-refs] FAIL: not in a git repo"
    exit 2
}
cd "$REPO_ROOT"

ALLOWLIST="scripts/doc-refs-allowlist"

# Collect matches: file:line:notes/path. grep -r -E -n -o searches
# recursively, prints each match (not the whole matched line) prefixed
# by file:line.
matches=$(grep -rEno 'notes/[a-z][a-zA-Z0-9_/-]*\.md' \
    --exclude-dir='.git' \
    --exclude-dir='Externals' \
    --exclude-dir='doc' \
    --exclude-dir='bin' \
    --exclude-dir='build' \
    --exclude-dir='build-release' \
    --exclude-dir='build-lsp' \
    --exclude-dir='build-profile' \
    --exclude-dir='build-tests' \
    --exclude-dir='_workspace' \
    --exclude-dir='dist' \
    --exclude-dir='.cache' \
    --exclude-dir='.remember' \
    . 2>/dev/null \
    | sed 's|^\./||' \
    | grep -vE '^(\.claude/worktrees/|notes/archive/|scripts/check-doc-refs\.sh:|\.git/|\.remember/)' \
    || true)

if [ -z "$matches" ]; then
    echo "[check-doc-refs] OK: no notes/ refs found"
    exit 0
fi

fail=0
broken_count=0
checked_count=0

while IFS= read -r line; do
    [ -z "$line" ] && continue
    src=$(echo "$line" | cut -d: -f1)
    src_line=$(echo "$line" | cut -d: -f2)
    ref=$(echo "$line" | cut -d: -f3-)
    checked_count=$((checked_count + 1))

    # Existence check (relative to repo root).
    if [ -f "$ref" ] || [ -d "$ref" ]; then
        continue
    fi

    # Allowlist check.
    if [ -f "$ALLOWLIST" ] && grep -qxF "$ref" "$ALLOWLIST"; then
        continue
    fi

    if [ "$fail" -eq 0 ]; then
        echo "[check-doc-refs] FAIL: dead doc references found:"
    fi
    echo "    $src:$src_line → $ref"
    broken_count=$((broken_count + 1))
    fail=1
done <<< "$matches"

if [ "$fail" -ne 0 ]; then
    echo
    echo "[check-doc-refs] $broken_count broken / $checked_count total"
    echo "[check-doc-refs] If any of these are intentional dangling refs,"
    echo "[check-doc-refs] add the path (one per line) to $ALLOWLIST"
    exit 1
fi

echo "[check-doc-refs] OK: $checked_count notes/ refs all resolve"
exit 0
