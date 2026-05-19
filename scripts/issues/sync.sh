#!/usr/bin/env bash
# Sync recent commits to GitHub issues.
#
# For every commit since the last sync (cursor stored in
# .git/last-issue-sync), scan the commit message for references in the
# form "#N", "Refs #N", "Closes #N", "Fixes #N", "Resolves #N". For each
# referenced issue:
#   - leave a comment linking to the commit
#   - if the keyword is closing-style (Closes/Fixes/Resolves), close
#     the issue
#
# Usage:
#   scripts/issues/sync.sh                 # sync since cursor
#   scripts/issues/sync.sh --since <sha>   # sync from sha forward (overrides cursor)
#   scripts/issues/sync.sh --dry-run       # print what would happen
#
# Requirements: gh CLI authenticated for the target repo.

set -eu
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

REPO="${OPENXRAY_ISSUES_REPO:-blackden/xray-16}"
CURSOR_FILE=".git/last-issue-sync"
DRY=0
SINCE=""

while [ $# -gt 0 ]; do
    case "$1" in
        --dry-run)  DRY=1; shift ;;
        --since)    SINCE="$2"; shift 2 ;;
        -h|--help)
            sed -n '2,/^$/p' "$0" | sed 's/^# \?//'
            exit 0
            ;;
        *)  echo "unknown arg: $1" >&2; exit 1 ;;
    esac
done

if [ -z "$SINCE" ]; then
    if [ -f "$CURSOR_FILE" ]; then
        SINCE=$(cat "$CURSOR_FILE")
    else
        # First run: only look at HEAD to avoid spamming all history at once.
        SINCE=$(git rev-parse HEAD^ 2>/dev/null || git rev-parse HEAD)
    fi
fi

HEAD_SHA=$(git rev-parse HEAD)
if [ "$SINCE" = "$HEAD_SHA" ]; then
    echo "==> Already at $HEAD_SHA, nothing to sync."
    exit 0
fi

GH="$(command -v gh || echo /opt/homebrew/bin/gh)"
if ! "$GH" auth status >/dev/null 2>&1; then
    echo "ERROR: gh CLI not authenticated. Run 'gh auth login'." >&2
    exit 1
fi

echo "==> Syncing commits ${SINCE}..${HEAD_SHA} to repo $REPO"

# `git log ... --reverse` lists oldest-first so comments land in commit order.
commits=$(git log --reverse --pretty=format:'%H' "${SINCE}..${HEAD_SHA}")
if [ -z "$commits" ]; then
    echo "==> No new commits."
    exit 0
fi

note() { if [ "$DRY" = 1 ]; then echo "  [dry-run] $*"; else echo "  $*"; fi }

for sha in $commits; do
    short=$(git rev-parse --short "$sha")
    subject=$(git log -1 --pretty=format:'%s' "$sha")
    body=$(git log -1 --pretty=format:'%B' "$sha")

    # Extract every (keyword?, #N) pair. We accept Closes/Fixes/Resolves
    # (case-insensitive) as closers; bare "#N" / "Refs #N" / "See #N" stay open.
    # Use awk to walk through the body and emit "KEYWORD NN" lines.
    while IFS= read -r ref; do
        keyword=$(echo "$ref" | awk '{print toupper($1)}')
        num=$(echo "$ref" | awk '{print $2}')
        [ -z "$num" ] && continue

        commit_url="https://github.com/${REPO}/commit/${sha}"
        case "$keyword" in
            CLOSES|FIXES|RESOLVES|CLOSE|FIX|RESOLVE)
                comment="Addressed in [${short}](${commit_url}): ${subject}"
                note "Issue #$num <-- close via $short"
                if [ "$DRY" = 0 ]; then
                    "$GH" issue comment "$num" --repo "$REPO" --body "$comment" >/dev/null
                    "$GH" issue close "$num" --repo "$REPO" --reason completed >/dev/null
                fi
                ;;
            *)
                comment="Referenced in [${short}](${commit_url}): ${subject}"
                note "Issue #$num <-- ref via $short"
                if [ "$DRY" = 0 ]; then
                    "$GH" issue comment "$num" --repo "$REPO" --body "$comment" >/dev/null
                fi
                ;;
        esac
    done < <(echo "$body" | grep -oiE '(closes|fixes|resolves|refs|see|ref)?[[:space:]]*#[0-9]+' \
              | awk '{
                    if ($1 ~ /^#/) print "REFS", substr($1,2);
                    else           print toupper($1), substr($2,2);
                }')
done

if [ "$DRY" = 0 ]; then
    echo "$HEAD_SHA" > "$CURSOR_FILE"
    echo "==> Cursor advanced to $HEAD_SHA"
else
    echo "==> Dry run complete; cursor unchanged."
fi
