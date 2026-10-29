#!/usr/bin/env bash
#
# Tests runner script.
#
# Allow unicode quotes:
# shellcheck disable=SC1111

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
    >&2 printf '[FAIL] %s: missing expected stdout\n' "$NAME"
    (( failed += 1 ))
    return
  fi

  if [[ -f "$EXPECTED_EXITCODE_FILE" ]]; then
    expected_exit_code=$(< "$EXPECTED_EXITCODE_FILE")
  fi

  "$LISPICO_EXECUTABLE" "$SOURCE" \
    > "$TMP_DIR/stdout" \
    2> "$TMP_DIR/stderr" || actual_exit_code=$?

  if (( actual_exit_code != expected_exit_code )); then
    >&2 printf '  Exit status: expected “%s”, got “%s”\n' "$expected_exit_code" "$actual_exit_code"
    ok=false
  fi

  if ! cmp -s -- "$EXPECTED_OUT_FILE" "$TMP_DIR/stdout"; then
    >&2 printf '  Stdout differs:\n'
    diff -u -- "$EXPECTED_OUT_FILE" "$TMP_DIR/stdout" || true
    ok=false
  fi

  if [[ -f "$EXPECTED_ERR_FILE" ]]; then
    if ! cmp -s -- "$EXPECTED_ERR_FILE" "$TMP_DIR/stderr"; then
      >&2 printf '  Stderr differs:\n'
      diff -u -- "$EXPECTED_ERR_FILE" "$TMP_DIR/stderr" || true
      ok=false
    fi
  elif [[ -s "$TMP_DIR/stderr" ]]; then
    >&2 printf '  Unexpected stderr:\n'
    cat -- "$TMP_DIR/stderr"
    ok=false
  fi

  if [[ "$ok" == true ]]; then
    printf '[PASS] %s\n' "$NAME"
    (( passed += 1 ))
  else
    >&2 printf '[FAIL] %s\n' "$NAME"
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

  run_test "$source" "tests/expected/$name"
done

# Test all expected failures
for source in examples/fail/*.lisp; do
  # Get the name with parent directory path and *.lisp extension removed
  name="${source##*/}"
  name="${name%.lisp}"

  run_test "$source" "tests/expected/fail/$name"
done

printf '\nPassed: %d, Failed: %d\n' "$passed" "$failed"
[[ "$passed" -gt 0 && "$failed" -eq 0 ]]
