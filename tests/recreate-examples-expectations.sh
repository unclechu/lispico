#!/usr/bin/env bash
#
# Re-generate `tests/expected-examples/*.{stdout,stderr,exitcode}` files.
#
# WARNING! This does not mean those results are correct.
# Inspect the resulting files manually first.
#
# Also note that removed examples won’t result into automatically removing
# expectation files.
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

# Make sure wildcards are expanded instead of converted into plain text
shopt -s nullglob

# All existing example programs
for source in examples/*.lisp; do
  # Get the name with parent directory path and *.lisp extension removed
  name="${source##*/}"
  name="${name%.lisp}"
  exit_code=0

  "$LISPICO_EXECUTABLE" "$source" \
    > "tests/expected-examples/${name}.stdout" \
    2> "tests/expected-examples/${name}.stderr" || exit_code=$?

  printf %d "$exit_code" > "tests/expected-examples/${name}.exitcode"
done

# All expected failures
for source in examples/fail/*.lisp; do
  # Get the name with parent directory path and *.lisp extension removed
  name="${source##*/}"
  name="${name%.lisp}"
  exit_code=0

  "$LISPICO_EXECUTABLE" "$source" \
    > "tests/expected-examples/fail/${name}.stdout" \
    2> "tests/expected-examples/fail/${name}.stderr" || exit_code=$?

  printf '%d\n' "$exit_code" > "tests/expected-examples/fail/${name}.exitcode"
done

echo 'DONE'
