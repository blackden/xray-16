#!/usr/bin/env bash
set -euo pipefail

PHASE="local-build"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
LOG_DIR="${PROJECT_ROOT}/logs"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
LOG_FILE=""
LOG_ALREADY_PRINTED=0

print_log_location() {
    if (( LOG_ALREADY_PRINTED )); then
        return
    fi
    if [[ -n "${NO_LOG:-}" ]]; then
        printf '[local-build] Logging disabled (NO_LOG=1)\n'
    else
        printf '[local-build] Log saved to: %s\n' "${LOG_FILE}"
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

"${PROJECT_ROOT}/scripts/bootstrap-macos.sh"
"${PROJECT_ROOT}/scripts/build.sh" RelWithDebInfo

set +x
print_log_location
