#! /usr/bin/env bash
set -o errexit || exit; set -o errtrace; set -o nounset; set -o pipefail

: "${MAIN_SOURCE_FILE:=main.c}"
: "${OUTPUT_EXECUTABLE_NAME:=simple-lisp-c-interpreter}"

# CFLAGS=$(pkg-config --cflags foo)
# LIBS=$(pkg-config --libs foo)
CFLAGS=
LIBS=

: "${CC:=cc}"

# The variables substitution is intentionally preserving arguments splitting.
# shellcheck disable=SC2206
BUILD_CMD=(
  $CC -std=c23 -O2 -Wall -Wextra -Wpedantic $CFLAGS
  "$MAIN_SOURCE_FILE" -o "$OUTPUT_EXECUTABLE_NAME"
  $LIBS
)

set -o xtrace
"${BUILD_CMD[@]}" "$@"
