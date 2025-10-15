#!/usr/bin/env bash
set -euo pipefail

PHASE="run"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
LOG_DIR="${PROJECT_ROOT}/logs"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
LOG_FILE=""
LOG_ALREADY_PRINTED=0
CONFIG_NAME="RelWithDebInfo"

print_log_location() {
    if (( LOG_ALREADY_PRINTED )); then
        return
    fi
    if [[ -n "${NO_LOG:-}" ]]; then
        printf '[run] Logging disabled (NO_LOG=1)\n'
    else
        printf '[run] Log saved to: %s\n' "${LOG_FILE}"
    fi
    LOG_ALREADY_PRINTED=1
}

usage() {
    cat <<'EOF'
Usage: scripts/run-with-log.sh [--config <Debug|RelWithDebInfo|Release>] [--] [xr_3da args...]

Runs the selected build of xr_3da with -log and collects runtime logs into logs/run_<timestamp>/.
EOF
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

ARGS=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        -c|--config)
            if [[ $# -lt 2 ]]; then
                usage
                exit 1
            fi
            CONFIG_NAME="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            ARGS+=("$@")
            break
            ;;
        *)
            ARGS+=("$1")
            shift
            ;;
    esac
done

CONFIG_CANONICAL="$(printf '%s' "${CONFIG_NAME}" | tr '[:upper:]' '[:lower:]')"
case "${CONFIG_CANONICAL}" in
    debug)
        CONFIG_NAME="Debug"
        ;;
    relwithdebinfo|relwithdebuginfo|rwdi|releasewithdebuginfo)
        CONFIG_NAME="RelWithDebInfo"
        ;;
    release)
        CONFIG_NAME="Release"
        ;;
    *)
        error_msg="[run] Unknown configuration '${CONFIG_NAME}'. Supported values: Debug, RelWithDebInfo (default), Release."
        printf '%s\n' "${error_msg}" >&2
        exit 1
        ;;
esac

BUILD_DIR="${PROJECT_ROOT}/build/macos-arm64/${CONFIG_NAME}"
BIN_PATH="${BUILD_DIR}/bin/xr_3da"
if [[ ! -x "${BIN_PATH}" ]]; then
    printf '[run][error] Expected binary not found at %s\n' "${BIN_PATH}" >&2
    exit 1
fi

HAS_LOG_FLAG=0
for arg in "${ARGS[@]}"; do
    if [[ "${arg}" == "-log" ]]; then
        HAS_LOG_FLAG=1
        break
    fi
done
if (( ! HAS_LOG_FLAG )); then
    ARGS+=("-log")
fi

log_run_dir="${LOG_DIR}/run_${TIMESTAMP}"
mkdir -p "${log_run_dir}"

"${BIN_PATH}" "${ARGS[@]}"
XRAY_EXIT=$?

APPDATA_DIR="${HOME}/stalker_cop/_appdata_/logs"
if [[ -d "${APPDATA_DIR}" ]]; then
    shopt -s nullglob
    COPIED_FILES=()
    for file in "${APPDATA_DIR}"/xray_*.log; do
        dest="${log_run_dir}/$(basename "${file}")"
        cp "${file}" "${dest}"
        COPIED_FILES+=("${dest}")
    done
    shopt -u nullglob
    if (( ${#COPIED_FILES[@]} > 0 )); then
        set +x
        printf '[run] Runtime logs copied to %s:\n' "${log_run_dir}"
        for file in "${COPIED_FILES[@]}"; do
            printf '  %s\n' "${file}"
        done
        set -x
    else
        set +x
        printf '[run] No runtime logs found in %s\n' "${APPDATA_DIR}"
        set -x
    fi
else
    set +x
    printf '[run] Runtime log directory not found: %s\n' "${APPDATA_DIR}"
    set -x
fi

set +x
printf '[run] Binary: %s\n' "${BIN_PATH}"
printf '[run] Log directory: %s\n' "${log_run_dir}"
print_log_location
exit "${XRAY_EXIT}"
