#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ENV_FILE="${SCRIPT_DIR}/.macos-build-env"

log() {
    printf '[build] %s\n' "$*"
}

error() {
    printf '[build][error] %s\n' "$*" >&2
}

if [[ "$(uname -s)" != "Darwin" ]]; then
    error "This build script is intended for macOS only."
    exit 1
fi

if [[ -z "${CMAKE_BUILD_PARALLEL_LEVEL:-}" && -f "${ENV_FILE}" ]]; then
    # shellcheck disable=SC1090
    source "${ENV_FILE}"
    log "Loaded CMAKE_BUILD_PARALLEL_LEVEL from ${ENV_FILE} (${CMAKE_BUILD_PARALLEL_LEVEL})."
fi

CONFIG_INPUT="${1:-RelWithDebInfo}"
CONFIG_CANONICAL="$(printf '%s' "${CONFIG_INPUT}" | tr '[:upper:]' '[:lower:]')"
case "${CONFIG_CANONICAL}" in
    debug)
        CONFIG_NAME="Debug"
        CONFIG_PRESET="macos-arm64-debug"
        BUILD_PRESET="macos-arm64-debug-build"
        ;;
    relwithdebinfo|relwithdebuginfo|rwdi|releasewithdebuginfo)
        CONFIG_NAME="RelWithDebInfo"
        CONFIG_PRESET="macos-arm64-relwithdebinfo"
        BUILD_PRESET="macos-arm64-relwithdebinfo-build"
        ;;
    release)
        CONFIG_NAME="Release"
        CONFIG_PRESET="macos-arm64-release"
        BUILD_PRESET="macos-arm64-release-build"
        ;;
    *)
        error "Unknown configuration '${CONFIG_INPUT}'. Supported values: Debug, RelWithDebInfo (default), Release."
        exit 1
        ;;
esac

log "Configuring (${CONFIG_NAME})…"
(cd "${PROJECT_ROOT}" && cmake --preset "${CONFIG_PRESET}")

log "Building (${CONFIG_NAME})…"
(cd "${PROJECT_ROOT}" && cmake --build --preset "${BUILD_PRESET}")

log "Build complete. Artifacts are in build/macos-arm64/${CONFIG_NAME}."
