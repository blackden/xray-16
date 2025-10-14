#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ENV_FILE="${SCRIPT_DIR}/.macos-build-env"

log() {
    printf '[bootstrap] %s\n' "$*"
}

error() {
    printf '[bootstrap][error] %s\n' "$*" >&2
}

if [[ "$(uname -s)" != "Darwin" ]]; then
    error "This bootstrap script is intended for macOS only."
    exit 1
fi

if ! xcode-select -p >/dev/null 2>&1; then
    error "Xcode Command Line Tools are not installed. Run 'xcode-select --install' first."
    exit 1
fi

if ! command -v brew >/dev/null 2>&1; then
    error "Homebrew is not installed. Visit https://brew.sh/ and install it, then re-run this script."
    exit 1
fi

log "Ensuring Homebrew dependencies are installed…"
brew bundle --file="${PROJECT_ROOT}/Brewfile"

if [[ -n "${CMAKE_BUILD_PARALLEL_LEVEL:-}" ]]; then
    log "CMAKE_BUILD_PARALLEL_LEVEL already set to ${CMAKE_BUILD_PARALLEL_LEVEL}."
    printf 'export CMAKE_BUILD_PARALLEL_LEVEL=%s\n' "${CMAKE_BUILD_PARALLEL_LEVEL}" > "${ENV_FILE}"
else
    CORES="$(sysctl -n hw.ncpu)"
    export CMAKE_BUILD_PARALLEL_LEVEL="${CORES}"
    log "CMAKE_BUILD_PARALLEL_LEVEL was unset. Defaulting to detected core count: ${CORES}."
    printf 'export CMAKE_BUILD_PARALLEL_LEVEL=%s\n' "${CMAKE_BUILD_PARALLEL_LEVEL}" > "${ENV_FILE}"
fi

log "Saved environment defaults to ${ENV_FILE}."
log "Bootstrap complete."
