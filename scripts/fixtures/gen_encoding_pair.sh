#!/usr/bin/env bash
# Generate cp1251 / cp1250 / utf8 fixture trio from a UTF-8 source phrase.
# Used to populate tests/fixtures/encoding/ for the UTF-8 migration tests.
#
# Usage:
#   scripts/fixtures/gen_encoding_pair.sh <name> <utf8-phrase>
#
# Example:
#   scripts/fixtures/gen_encoding_pair.sh skadovsk 'прибытие на Скадовск'

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <name> <utf8-phrase>" >&2
    exit 2
fi

NAME="$1"
PHRASE="$2"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="${REPO_ROOT}/tests/fixtures/encoding"
mkdir -p "$OUT_DIR"

printf '%s' "$PHRASE" > "${OUT_DIR}/${NAME}.utf8"
iconv -f UTF-8 -t CP1251 < "${OUT_DIR}/${NAME}.utf8" > "${OUT_DIR}/${NAME}.cp1251"
iconv -f UTF-8 -t CP1250//TRANSLIT < "${OUT_DIR}/${NAME}.utf8" > "${OUT_DIR}/${NAME}.cp1250" 2>/dev/null \
    || cp "${OUT_DIR}/${NAME}.utf8" "${OUT_DIR}/${NAME}.cp1250"

echo "generated:"
ls -la "${OUT_DIR}/${NAME}".*
