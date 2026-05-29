#!/usr/bin/env bash
# publish-to-github.sh — публикация sanitized зеркала на GitHub.
#
# Берёт текущий tip macos/blackden/master на gitea (origin),
# клонирует свежий, удаляет internal пути из всей истории через
# git-filter-repo, и force-push'ит результат на github-backup как master.
#
# Internal пути (НЕ публикуются):
#   - CLAUDE.md            — инструкции для Claude
#   - .claude/             — agents, settings, worktrees, locks
#   - .remember/           — Claude session history
#   - notes/strategy/      — management, mentor, creator-voice, code-craft,
#                            content-system, progress, roadmap-*
#
# Что публикуется:
#   - весь src/, scripts/, Makefile, Brewfile, CMake
#   - notes/playbooks/  — bug-patterns, gotchas, smoke-tests
#   - notes/decisions/  — все технические design docs
#   - notes/reference/  — engine-map, architecture, apple-silicon, macos-*
#
# Использование:
#   ./scripts/publish-to-github.sh
#
# Требования:
#   - git-filter-repo (brew install git-filter-repo)
#   - SSH доступ к github.com/blackden/xray-16

set -euo pipefail

# --- config ---
GITEA_REMOTE="git@git.fedorov.tech:ragnar/xray-16.git"
GITHUB_REMOTE="git@github.com:blackden/xray-16.git"
SRC_BRANCH="macos/blackden/master"
DEST_BRANCH="master"
WORKDIR="$(mktemp -d /tmp/xray-16-public-mirror.XXXXXX)"

# Пути для исключения из истории
INTERNAL_PATHS=(
    "CLAUDE.md"
    ".claude"
    ".remember"
    "notes/strategy"
)

# --- preflight ---
if ! command -v git-filter-repo >/dev/null 2>&1; then
    echo "ERROR: git-filter-repo не установлен. brew install git-filter-repo" >&2
    exit 1
fi

trap 'rm -rf "$WORKDIR"' EXIT

# --- clone fresh ---
echo "==> Свежий clone из $GITEA_REMOTE в $WORKDIR"
git clone --no-local --branch "$SRC_BRANCH" "$GITEA_REMOTE" "$WORKDIR/repo"
cd "$WORKDIR/repo"

# --- filter-repo: убрать internal пути из всех commits ---
FILTER_ARGS=()
for p in "${INTERNAL_PATHS[@]}"; do
    FILTER_ARGS+=(--path "$p")
done

echo "==> git-filter-repo --invert-paths ${FILTER_ARGS[*]}"
git-filter-repo --invert-paths "${FILTER_ARGS[@]}" --force

# --- add github remote + push ---
echo "==> Добавляю github remote"
git remote add github "$GITHUB_REMOTE"

# Force-push потому что history переписана (sha все новые)
echo "==> Force-push на github как $DEST_BRANCH (sanitized history)"
git push --force github "HEAD:refs/heads/$DEST_BRANCH"

# --- summary ---
COMMIT_COUNT=$(git rev-list --count HEAD)
HEAD_SHA=$(git rev-parse --short HEAD)
echo ""
echo "==> Done"
echo "    Source: $SRC_BRANCH @ $(git -C "$WORKDIR/repo" log -1 --format=%h@%s 2>/dev/null || echo "?")"
echo "    Mirror: github $DEST_BRANCH @ $HEAD_SHA ($COMMIT_COUNT commits)"
echo "    URL:    https://github.com/blackden/xray-16/tree/$DEST_BRANCH"
