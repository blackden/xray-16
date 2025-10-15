#!/usr/bin/env bash
set -euo pipefail

PHASE="bootstrap"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ENV_FILE="${SCRIPT_DIR}/.macos-build-env"
LOG_DIR="${PROJECT_ROOT}/logs"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
LOG_FILE=""
LOG_ALREADY_PRINTED=0

log() {
    printf '[bootstrap] %s\n' "$*"
}

error() {
    printf '[bootstrap][error] %s\n' "$*" >&2
}

print_log_location() {
    if (( LOG_ALREADY_PRINTED )); then
        return
    fi
    if [[ -n "${NO_LOG:-}" ]]; then
        printf '[bootstrap] Logging disabled (NO_LOG=1)\n'
    else
        printf '[bootstrap] Log saved to: %s\n' "${LOG_FILE}"
    fi
    LOG_ALREADY_PRINTED=1
}

if [[ -z "${NO_LOG:-}" ]]; then
    mkdir -p "${LOG_DIR}"
    LOG_FILE="${LOG_DIR}/${PHASE}_${TIMESTAMP}.log"
    # shellcheck disable=SC2064
    trap 'EXIT_CODE=$?; { set +x; } 2>/dev/null; print_log_location; exit $EXIT_CODE' EXIT
    exec > >(tee -a "${LOG_FILE}")
    exec 2> >(tee -a "${LOG_FILE}" >&2)
else
    # shellcheck disable=SC2064
    trap 'EXIT_CODE=$?; { set +x; } 2>/dev/null; print_log_location; exit $EXIT_CODE' EXIT
fi

set -x

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

log "Environment snapshot:"
log "macOS version: $(sw_vers -productVersion || echo 'unknown')"
log "Kernel: $(uname -a)"
if command -v clang >/dev/null 2>&1; then
    clang --version | head -n 1
else
    log "clang not found in PATH"
fi
if command -v cmake >/dev/null 2>&1; then
    cmake --version | head -n 1
else
    log "cmake not found in PATH"
fi
if command -v xcodebuild >/dev/null 2>&1; then
    xcodebuild -version
else
    log "xcodebuild not found in PATH"
fi
xcrun --show-sdk-version 2>/dev/null || log "xcrun --show-sdk-version failed"
brew --version
brew list --versions cmake ninja sdl2 libogg libvorbis theora lzo mimalloc jpeg-turbo openal-soft || true

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

print_log_location
