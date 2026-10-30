#!/usr/bin/env bash
#
# Tests runner for a C test files.
#
# Allow unicode quotes:
# shellcheck disable=SC1111
# Do not assume environment variables presence:
# shellcheck enable=check-unassigned-uppercase

set -o errexit || exit; set -o errtrace; set -o nounset; set -o pipefail

SCRIPT_DIR=$(dirname -- "${BASH_SOURCE[0]}")
cd -- "$SCRIPT_DIR/.." # Switch to project root

# Terminal colors
# Disable stdout terminal coloring for redirects
if [[ ! -t 1 && ${FORCE_COLOR:-0} != 1 && ${GITHUB_ACTIONS:-} != true ]]; then
  O_RESET_COLOR=
  O_SECONDARY_COLOR=
  O_FAIL_COLOR=
  O_PASS_COLOR=
else
  O_RESET_COLOR=$'\033[0m'
  O_SECONDARY_COLOR=$'\033[1m'
  O_FAIL_COLOR=$'\033[1;37;41m'
  O_PASS_COLOR=$'\033[32m'
fi
# Disable stderr terminal coloring for redirects
if [[ ! -t 2 && ${FORCE_COLOR:-0} != 1 && ${GITHUB_ACTIONS:-} != true ]]; then
  E_RESET_COLOR=
  # E_SECONDARY_COLOR=
  E_FAIL_COLOR=
  # E_PASS_COLOR=
else
  E_RESET_COLOR=$'\033[0m'
  # E_SECONDARY_COLOR=$'\033[1m'
  E_FAIL_COLOR=$'\033[1;37;41m'
  # E_PASS_COLOR=$'\033[32m'
fi

passed=0
failed=0

run-c-test() {
  if (( $# != 1 )) || [[ ! -s $1 ]]; then
    >&2 printf \
      '%s[CRITICAL FAIL]%s run-c-test expects a single argument with a C test source file!\n' \
      "$E_FAIL_COLOR" "$E_RESET_COLOR"
    return 1
  fi

  local MAIN_SOURCE_FILE=$1
  echo '————————————————————————————————————————————————————————————————————————'
  printf 'Running “%s” C test…\n' "$MAIN_SOURCE_FILE"

  local NAME=${MAIN_SOURCE_FILE##*/}; NAME=${NAME%.c}

  local OUTPUT_EXECUTABLE_NAME="./lispico-$NAME-tests"

  local exit_code=0

  (
    set -o xtrace
    MAIN_SOURCE_FILE="$MAIN_SOURCE_FILE" OUTPUT_EXECUTABLE_NAME="$OUTPUT_EXECUTABLE_NAME" ./build.sh
  ) || exit_code=$?
  if (( exit_code != 0 )); then
    >&2 printf '%s[FAIL]%s C test “%s” failed to build\n' \
      "$E_FAIL_COLOR" "$E_RESET_COLOR" "$MAIN_SOURCE_FILE"
    (( failed += 1 ))
    return 0
  fi

  (set -o xtrace; "$OUTPUT_EXECUTABLE_NAME") || exit_code=$?
  if (( exit_code != 0 )); then
    >&2 printf '%s[FAIL]%s C test “%s” did not pass\n' \
      "$E_FAIL_COLOR" "$E_RESET_COLOR" "$MAIN_SOURCE_FILE"
    (( failed += 1 ))
    return 0
  fi

  printf '%s[PASS]%s C test “%s” passed\n' "$O_PASS_COLOR" "$O_RESET_COLOR" "$MAIN_SOURCE_FILE"
  (( passed += 1 ))
}

if (( $# == 0 )); then
  # Make sure wildcards are expanded instead of converted into plain text
  shopt -s nullglob

  # Run all C tests
  for c_test_src in tests/c/*.c; do
    run-c-test "$c_test_src"
  done
elif (( $# == 1 )) && [[ -s $1 ]]; then
  run-c-test "$1"
else
  >&2 printf '%s[CRITICAL FAIL]%s Unexpected arguments!\n' "$E_FAIL_COLOR" "$E_RESET_COLOR"
  exit 1
fi

if (( failed != 0 )); then
  PASS_HL=$O_SECONDARY_COLOR
  FAIL_HL=$O_FAIL_COLOR
else
  PASS_HL=$O_PASS_COLOR
  FAIL_HL=$O_SECONDARY_COLOR
fi

echo '————————————————————————————————————————————————————————————————————————'

printf '\nC tests summary: %sPassed: %d%s, %sFailed: %d%s\n' \
  "$PASS_HL" "$passed" "$O_RESET_COLOR" \
  "$FAIL_HL" "$failed" "$O_RESET_COLOR"

# Final exit code
[[ "$passed" -gt 0 && "$failed" -eq 0 ]]
