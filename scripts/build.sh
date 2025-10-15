#!/usr/bin/env bash
set -euo pipefail

PHASE="build"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ENV_FILE="${SCRIPT_DIR}/.macos-build-env"
LOG_DIR="${PROJECT_ROOT}/logs"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
LOG_FILE=""
LOG_ALREADY_PRINTED=0
CONFIG_NAME=""
CMAKE_OUTPUT_COPY=""
CMAKE_ERROR_COPY=""

log() {
    printf '[build] %s\n' "$*"
}

error() {
    printf '[build][error] %s\n' "$*" >&2
}

print_log_location() {
    if (( LOG_ALREADY_PRINTED )); then
        return
    fi
    if [[ -n "${NO_LOG:-}" ]]; then
        printf '[build] Logging disabled (NO_LOG=1)\n'
    else
        printf '[build] Log saved to: %s\n' "${LOG_FILE}"
    fi
    LOG_ALREADY_PRINTED=1
}

if [[ -z "${NO_LOG:-}" ]]; then
    mkdir -p "${LOG_DIR}"
    LOG_FILE="${LOG_DIR}/${PHASE}_${TIMESTAMP}.log"
    ln -sfn "${LOG_FILE}" "${LOG_DIR}/latest_build.log"
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
    error "This build script is intended for macOS only."
    exit 1
fi

if [[ -z "${CMAKE_BUILD_PARALLEL_LEVEL:-}" && -f "${ENV_FILE}" ]]; then
    # shellcheck disable=SC1090
    source "${ENV_FILE}"
    log "Loaded CMAKE_BUILD_PARALLEL_LEVEL from ${ENV_FILE} (${CMAKE_BUILD_PARALLEL_LEVEL})."
fi
log "Parallel level: ${CMAKE_BUILD_PARALLEL_LEVEL:-auto}"

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

log "===== CONFIGURE (${CONFIG_NAME}) ====="
CONFIG_RESULT=0
if ! (cd "${PROJECT_ROOT}" && cmake --preset "${CONFIG_PRESET}"); then
    CONFIG_RESULT=$?
fi

BUILD_DIR="${PROJECT_ROOT}/build/macos-arm64/${CONFIG_NAME}"
if [[ -z "${NO_LOG:-}" ]]; then
    if [[ -f "${BUILD_DIR}/CMakeFiles/CMakeOutput.log" ]]; then
        CMAKE_OUTPUT_COPY="${LOG_DIR}/cmake-output_${CONFIG_NAME}_${TIMESTAMP}.log"
        cp "${BUILD_DIR}/CMakeFiles/CMakeOutput.log" "${CMAKE_OUTPUT_COPY}"
    fi
    if [[ -f "${BUILD_DIR}/CMakeFiles/CMakeError.log" ]]; then
        CMAKE_ERROR_COPY="${LOG_DIR}/cmake-error_${CONFIG_NAME}_${TIMESTAMP}.log"
        cp "${BUILD_DIR}/CMakeFiles/CMakeError.log" "${CMAKE_ERROR_COPY}"
    fi
fi

if [[ ${CONFIG_RESULT} -ne 0 ]]; then
    error "Configuration failed (exit code ${CONFIG_RESULT})."
    exit "${CONFIG_RESULT}"
fi

log "===== BUILD (${CONFIG_NAME}) ====="
BUILD_ARGS=(--preset "${BUILD_PRESET}")
if [[ -n "${CMAKE_BUILD_PARALLEL_LEVEL:-}" ]]; then
    BUILD_ARGS+=(--parallel "${CMAKE_BUILD_PARALLEL_LEVEL}")
fi
(cd "${PROJECT_ROOT}" && cmake --build "${BUILD_ARGS[@]}")

log "===== SUMMARY (${CONFIG_NAME}) ====="
BIN_DIR="${BUILD_DIR}/bin"
set +x
printf '[build] Binary directory: %s\n' "${BIN_DIR}"
if [[ -n "${CMAKE_OUTPUT_COPY}" ]]; then
    printf '[build] Copied CMakeOutput.log to: %s\n' "${CMAKE_OUTPUT_COPY}"
fi
if [[ -n "${CMAKE_ERROR_COPY}" ]]; then
    printf '[build] Copied CMakeError.log to: %s\n' "${CMAKE_ERROR_COPY}"
fi
print_log_location
