#!/usr/bin/env bash
#
# Tests runner script.
#
# Allow unicode quotes:
# shellcheck disable=SC1111
# Do not assume environment variables presence:
# shellcheck enable=check-unassigned-uppercase

set -o errexit || exit; set -o errtrace; set -o nounset; set -o pipefail

SCRIPT_DIR=$(dirname -- "${BASH_SOURCE[0]}")
cd -- "$SCRIPT_DIR/.." # Switch to project root

: "${LISPICO_EXECUTABLE:=./lispico}"

# Guard dependencies
if [[ ! -x "$LISPICO_EXECUTABLE" ]]; then
  >&2 printf "“%s” is not an executable (did you run “./build.sh” first?)\n" "$LISPICO_EXECUTABLE"
  exit 1
fi
>/dev/null type cmp
>/dev/null type diff
>/dev/null type mktemp
>/dev/null type rm
>/dev/null type cat

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

cleanup() {
  rm -rfv -- "$TMP_DIR"
}

TMP_DIR="$(mktemp -d)"
trap cleanup EXIT

passed=0
failed=0

run_test() {
  set -o errexit || exit; set -o errtrace; set -o nounset; set -o pipefail

  if (( $# != 2 )); then
    >&2 printf \
      '[CRITICAL FAIL] run_test: Unexpected amount of arguments, expects exactly 2 but got: %d\n' \
      "$#"
    return 1
  fi

  local SOURCE="$1"
  local EXPECTED="$2"
  local NAME="${SOURCE#./}"

  local EXPECTED_OUT_FILE=${EXPECTED}.stdout
  local EXPECTED_ERR_FILE=${EXPECTED}.stderr
  local EXPECTED_EXITCODE_FILE=${EXPECTED}.exitcode

  local expected_exit_code=0
  local actual_exit_code=0
  local ok=true

  if [[ ! -f "$EXPECTED_OUT_FILE" ]]; then
    >&2 printf '%s[FAIL]%s %s: missing expected stdout\n' "$E_FAIL_COLOR" "$E_RESET_COLOR" "$NAME"
    (( failed += 1 ))
    return
  fi

  if [[ -f "$EXPECTED_EXITCODE_FILE" ]]; then
    expected_exit_code=$(< "$EXPECTED_EXITCODE_FILE")
  fi

  "$LISPICO_EXECUTABLE" "$SOURCE" >"$TMP_DIR/stdout" 2>"$TMP_DIR/stderr" || actual_exit_code=$?

  if (( actual_exit_code != expected_exit_code )); then
    >&2 printf '  Exit status: expected “%s”, got “%s”\n' "$expected_exit_code" "$actual_exit_code"
    ok=false
  fi

  if ! cmp -s -- "$EXPECTED_OUT_FILE" "$TMP_DIR/stdout"; then
    >&2 printf '  Stdout differs:\n'
    >&2 diff -u -- "$EXPECTED_OUT_FILE" "$TMP_DIR/stdout" || true
    ok=false
  fi

  if [[ -f "$EXPECTED_ERR_FILE" ]]; then
    if ! cmp -s -- "$EXPECTED_ERR_FILE" "$TMP_DIR/stderr"; then
      >&2 printf '  Stderr differs:\n'
      >&2 diff -u -- "$EXPECTED_ERR_FILE" "$TMP_DIR/stderr" || true
      ok=false
    fi
  elif [[ -s "$TMP_DIR/stderr" ]]; then
    >&2 printf '  Unexpected stderr:\n'
    >&2 cat -- "$TMP_DIR/stderr"
    ok=false
  fi

  if [[ "$ok" == true ]]; then
    printf '%s[PASS]%s %s\n' "$O_PASS_COLOR" "$O_RESET_COLOR" "$NAME"
    (( passed += 1 ))
  else
    >&2 printf '%s[FAIL]%s %s\n' "$E_FAIL_COLOR" "$E_RESET_COLOR" "$NAME"
    (( failed += 1 ))
  fi
}

# Make sure wildcards are expanded instead of converted into plain text
shopt -s nullglob

# Test all existing example programs
for source in examples/*.lisp; do
  # Get the name with parent directory path and *.lisp extension removed
  name="${source##*/}"
  name="${name%.lisp}"

  run_test "$source" "tests/expected-examples/$name"
done

# Test all expected failures
for source in examples/fail/*.lisp; do
  # Get the name with parent directory path and *.lisp extension removed
  name="${source##*/}"
  name="${name%.lisp}"

  run_test "$source" "tests/expected-examples/fail/$name"
done

if (( failed != 0 )); then
  PASS_HL=$O_SECONDARY_COLOR
  FAIL_HL=$O_FAIL_COLOR
else
  PASS_HL=$O_PASS_COLOR
  FAIL_HL=$O_SECONDARY_COLOR
fi

echo '————————————————————————————————————————————————————————————————————————'

printf '\n%sPassed: %d%s, %sFailed: %d%s\n' \
  "$PASS_HL" "$passed" "$O_RESET_COLOR" \
  "$FAIL_HL" "$failed" "$O_RESET_COLOR"

# Final exit code
[[ "$passed" -gt 0 && "$failed" -eq 0 ]]
