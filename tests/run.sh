#!/usr/bin/env bash
#
# Run all test suites sequentially.
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

echo '=== Running examples tests ============================================='
if (set -o xtrace; tests/run-examples.sh); then
  printf '%s[PASS]%s Examples tests passed\n' "$O_PASS_COLOR" "$O_RESET_COLOR"
  (( passed += 1 ))
else
  >&2 printf '%s[FAIL]%s Examples tests failed\n' "$E_FAIL_COLOR" "$E_RESET_COLOR"
  (( failed += 1 ))
fi

echo '=== Running C tests ===================================================='
if (set -o xtrace; tests/run-c.sh); then
  printf '%s[PASS]%s C tests passed\n' "$O_PASS_COLOR" "$O_RESET_COLOR"
  (( passed += 1 ))
else
  >&2 printf '%s[FAIL]%s C tests failed\n' "$E_FAIL_COLOR" "$E_RESET_COLOR"
  (( failed += 1 ))
fi

if (( failed != 0 )); then
  PASS_HL=$O_SECONDARY_COLOR
  FAIL_HL=$O_FAIL_COLOR
else
  PASS_HL=$O_PASS_COLOR
  FAIL_HL=$O_SECONDARY_COLOR
fi

echo '========================================================================'

printf 'Test suites summary: %sPassed: %d%s, %sFailed: %d%s\n' \
  "$PASS_HL" "$passed" "$O_RESET_COLOR" \
  "$FAIL_HL" "$failed" "$O_RESET_COLOR"

# Final exit code
[[ "$passed" -gt 0 && "$failed" -eq 0 ]]
