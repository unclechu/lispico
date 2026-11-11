#!/usr/bin/env bash
#
# Exercise the actual Lispico executable’s CLI, including evaluation and
# interpreter-owned copies of values parsed from process arguments.
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
  >&2 printf '“%s” is not executable (did you run “./build.sh” first?)\n' "$LISPICO_EXECUTABLE"
  exit 1
fi
>/dev/null type mktemp
>/dev/null type grep
>/dev/null type rm
>/dev/null type timeout
>/dev/null type cmp
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

TMP_DIR="$(mktemp -d)"
cleanup() { rm -rfv -- "$TMP_DIR"; }
trap cleanup EXIT

# Different source files let each invocation inspect just one result.
printf 'answer\n' > "$TMP_DIR/answer.lisp"
printf 'numbers\n' > "$TMP_DIR/numbers.lisp"
printf 'names\n' > "$TMP_DIR/names.lisp"
printf 'values\n' > "$TMP_DIR/values.lisp"
printf 'args\n' > "$TMP_DIR/args.lisp"
printf 'argc\n' > "$TMP_DIR/argc.lisp"
printf '(arr-get argv 1)\n' > "$TMP_DIR/argv-one.lisp"
printf '(is-frozen "answer")\n' > "$TMP_DIR/frozen-answer.lisp"
printf '(is-frozen "numbers")\n' > "$TMP_DIR/frozen-numbers.lisp"
printf '(arr-set "numbers" 0 999)\n' > "$TMP_DIR/mutate-numbers.lisp"
printf '(set "answer" 999)\n' > "$TMP_DIR/mutate-answer.lisp"
printf '(str-set "answer" 0 90)\n(arr-get argv 4)\n' > "$TMP_DIR/string-copy.lisp"
printf '(exit 7)\n' > "$TMP_DIR/exit.lisp"
printf '123\n' > "$TMP_DIR/stdin.lisp"

passed=0
failed=0

# Compare the complete stdout including final newlines. On successful runs
# stderr must be empty; on failures require an informative diagnostic.
run_case() {
  set -o errexit || exit; set -o errtrace; set -o nounset; set -o pipefail

  local name=$1
  local expected_status=$2
  local expected_stdout=$3
  local expected_error=$4
  shift 4

  local actual_status=0
  if timeout -- 15s "$LISPICO_EXECUTABLE" "$@" \
    > "$TMP_DIR/actual.stdout" 2> "$TMP_DIR/actual.stderr"; then
    actual_status=0
  else
    actual_status=$?
  fi

  printf '%s' "$expected_stdout" > "$TMP_DIR/expected.stdout"
  local ok=1
  if (( actual_status != expected_status )); then ok=0; fi
  if ! cmp -s -- "$TMP_DIR/expected.stdout" "$TMP_DIR/actual.stdout"; then ok=0; fi
  if [[ -n "$expected_error" ]]; then
    if ! grep -Fq -- "$expected_error" "$TMP_DIR/actual.stderr"; then ok=0; fi
  elif [[ -s "$TMP_DIR/actual.stderr" ]]; then
    ok=0
  fi

  if (( ok )); then
    printf '%s[PASS]%s %s\n' "$O_PASS_COLOR" "$O_RESET_COLOR" "$name"
    (( passed += 1 ))
  else
    >&2 printf '%s[FAIL]%s %s: expected exit %d, got %d\n' \
      "$E_FAIL_COLOR" "$E_RESET_COLOR" "$name" "$expected_status" "$actual_status"
    >&2 printf 'Expected stdout:\n'
    >&2 cat -- "$TMP_DIR/expected.stdout"
    >&2 printf 'Actual stdout:\n'
    >&2 cat -- "$TMP_DIR/actual.stdout"
    >&2 printf 'Actual stderr:\n'
    >&2 cat -- "$TMP_DIR/actual.stderr"
    (( failed += 1 ))
  fi
}

# Help is intentionally a special case: its leading executable name varies
# according to LISPICO_EXECUTABLE, so inspect the relevant usage lines.
run_help_case() {
  set -o errexit || exit; set -o errtrace; set -o nounset; set -o pipefail

  local name=$1
  shift

  local actual_status=0
  if timeout -- 15s "$LISPICO_EXECUTABLE" "$@" \
    > "$TMP_DIR/actual.stdout" 2> "$TMP_DIR/actual.stderr"; then
    actual_status=0
  else
    actual_status=$?
  fi

  if (( actual_status == 0 )) \
  && [[ ! -s "$TMP_DIR/actual.stderr" ]] \
  && grep -Fq -- 'Usage:' "$TMP_DIR/actual.stdout" \
  && grep -Fq -- '--arr-new' "$TMP_DIR/actual.stdout" \
  && grep -Fq -- '--freeze' "$TMP_DIR/actual.stdout"
  then
    printf '%s[PASS]%s %s\n' "$O_PASS_COLOR" "$O_RESET_COLOR" "$name"
    (( passed += 1 ))
  else
    >&2 printf '%s[FAIL]%s %s (exit %d)\n' "$E_FAIL_COLOR" "$E_RESET_COLOR" "$name" "$actual_status"
    >&2 cat -- "$TMP_DIR/actual.stdout" "$TMP_DIR/actual.stderr"
    (( failed += 1 ))
  fi
}

printf 'Testing CLI source selection and result printing…\n'
run_case 'no print result by default' 0 '' '' "$TMP_DIR/stdin.lisp"
run_case 'print final result' 0 $'SYM_VARIABLE(TYPE_U32(123))\n' '' \
  -p "$TMP_DIR/stdin.lisp"
run_case 'grouped -ps uses the following source path' 0 $'SYM_VARIABLE(TYPE_U32(123))\n' '' \
  -ps "$TMP_DIR/stdin.lisp"
run_case 'source before print flag' 0 $'SYM_VARIABLE(TYPE_U32(123))\n' '' \
  "$TMP_DIR/stdin.lisp" -p
run_case 'long source option' 0 $'SYM_VARIABLE(TYPE_U32(123))\n' '' \
  --print-result --source-file "$TMP_DIR/stdin.lisp"
run_case 'equals source option' 0 $'SYM_VARIABLE(TYPE_U32(123))\n' '' \
  -p "--source-file=$TMP_DIR/stdin.lisp"
run_case 'stdin by default' 0 $'SYM_VARIABLE(TYPE_U32(123))\n' '' \
  -p < "$TMP_DIR/stdin.lisp"
run_case 'stdin with -s -' 0 $'SYM_VARIABLE(TYPE_U32(123))\n' '' \
  -ps - < "$TMP_DIR/stdin.lisp"
run_case 'grouped -ps with process substitution' 0 $'SYM_VARIABLE(TYPE_U32(123))\n' '' \
  -ps <(printf '123\n')
run_case 'custom exit status remains unchanged by -p' 7 $'SYM_EMPTY\n' '' \
  -p "$TMP_DIR/exit.lisp"
run_help_case 'short help exits successfully' -h
run_help_case 'long help exits successfully' --help
run_help_case 'grouped help exits successfully' -ph

printf '\nTesting CLI variables and arrays…\n'
run_case 'u32 scalar' 0 $'SYM_VARIABLE(TYPE_U32(123))\n' '' \
  -p --var answer u32:123 "$TMP_DIR/answer.lisp"
run_case 'converted i32 scalar' 0 $'SYM_VARIABLE(TYPE_I32(123))\n' '' \
  -p --var answer i32:123 "$TMP_DIR/answer.lisp"
run_case 'converted f32 scalar' 0 $'SYM_VARIABLE(TYPE_F32(123.000000))\n' '' \
  -p --var answer f32:123 "$TMP_DIR/answer.lisp"
run_case 'typed pseudo-boolean' 0 $'SYM_VARIABLE(TYPE_I32(1))\n' '' \
  -p --var answer i32:true "$TMP_DIR/answer.lisp"
run_case 'scalar string with punctuation' 0 $'SYM_VARIABLE(TYPE_STRING("hello, [world]!"))\n' '' \
  -p --var answer 's:hello, [world]!' "$TMP_DIR/answer.lisp"
run_case 'empty scalar string' 0 $'SYM_VARIABLE(TYPE_STRING(""))\n' '' \
  -p --var answer s: "$TMP_DIR/answer.lisp"
run_case 'scalar string uses runtime-owned copy' 0 $'SYM_VARIABLE(TYPE_STRING("s:Alpha"))\n' '' \
  -p --var answer s:Alpha "$TMP_DIR/string-copy.lisp"
run_case 'dense u32 array' 0 $'SYM_ARRAY(ARRAY(TYPE_U32, [1, 2, 3, 4, 5, 6]))\n' '' \
  -p --arr numbers u32 6 1 2 3 4 5 6 "$TMP_DIR/numbers.lisp"
run_case 'dense i32 array' 0 $'SYM_ARRAY(ARRAY(TYPE_I32, [10, -20, 30]))\n' '' \
  -p --arr numbers i32 3 10 -20 30 "$TMP_DIR/numbers.lisp"
run_case 'dense f32 array' 0 $'SYM_ARRAY(ARRAY(TYPE_F32, [1.000000, 2.500000, -3.000000]))\n' '' \
  -p --arr numbers f32 3 1 2.5 -3 "$TMP_DIR/numbers.lisp"
run_case 'dense array with pseudo-booleans' 0 $'SYM_ARRAY(ARRAY(TYPE_U32, [1, 0, 1]))\n' '' \
  -p --arr numbers u32 3 true false true "$TMP_DIR/numbers.lisp"
run_case 'dense string array' 0 $'SYM_ARRAY(ARRAY(TYPE_STRING, ["Alice", "Bob", "Charlie"]))\n' '' \
  -p --arr names s 3 Alice Bob Charlie "$TMP_DIR/names.lisp"
run_case 'option-looking values and empty string in dense array' 0 \
  $'SYM_ARRAY(ARRAY(TYPE_STRING, ["[", "]", "--help", ""]))\n' '' \
  -p --arr values s 4 '[' ']' '--help' '' "$TMP_DIR/values.lisp"
run_case 'standalone -- is an array element when within count' 0 \
  $'SYM_ARRAY(ARRAY(TYPE_STRING, ["--", "-p"]))\n' '' \
  -p --arr values s 2 -- -p "$TMP_DIR/values.lisp"
run_case 'zero-length dense array' 0 $'SYM_ARRAY(ARRAY(TYPE_U32, []))\n' '' \
  -p --arr numbers u32 0 "$TMP_DIR/numbers.lisp"
run_case 'zero-length sparse array' 0 $'SYM_ARRAY(ARRAY(TYPE_U32, []))\n' '' \
  -p --arr-new numbers u32 0 "$TMP_DIR/numbers.lisp"
run_case 'sparse array preserves zero-filled gap' 0 \
  $'SYM_ARRAY(ARRAY(TYPE_U32, [123, 321, 0, 567]))\n' '' \
  -p --arr-new numbers u32 4 \
  --arr-set numbers 0 123 --arr-set numbers 1 321 --arr-set numbers 3 567 \
  "$TMP_DIR/numbers.lisp"
run_case 'sparse string array preserves empty gaps' 0 \
  $'SYM_ARRAY(ARRAY(TYPE_STRING, ["", "[middle]", ""]))\n' '' \
  -p --arr-new names s 3 --arr-set names 1 '[middle]' "$TMP_DIR/names.lisp"
run_case 'arr-set also works on dense arrays' 0 \
  $'SYM_ARRAY(ARRAY(TYPE_U32, [9, 2]))\n' '' \
  -p --arr numbers u32 2 1 2 --arr-set numbers 0 9 "$TMP_DIR/numbers.lisp"
run_case 'freeze scalar after definition' 0 $'SYM_VARIABLE(TYPE_U32(1))\n' '' \
  -p --var answer u32:100 --freeze answer "$TMP_DIR/frozen-answer.lisp"
run_case 'freeze array after initialization' 0 $'SYM_VARIABLE(TYPE_U32(1))\n' '' \
  -p --arr-new numbers u32 2 --arr-set numbers 0 7 --freeze numbers \
  "$TMP_DIR/frozen-numbers.lisp"
run_case 'frozen scalar cannot be changed by script' 1 '' 'is frozen' \
  --var answer u32:100 --freeze answer "$TMP_DIR/mutate-answer.lisp"
run_case 'frozen array cannot be changed by script' 1 '' 'is frozen' \
  --arr-new numbers u32 2 --freeze numbers "$TMP_DIR/mutate-numbers.lisp"

# These are the same quoted-array interpolation patterns documented in README.
numbers=(10 20 30 40)
run_case 'Bash numeric array interpolation' 0 \
  $'SYM_ARRAY(ARRAY(TYPE_U32, [10, 20, 30, 40]))\n' '' \
  -p --arr numbers u32 "${#numbers[@]}" "${numbers[@]}" "$TMP_DIR/numbers.lisp"
values=('hello, world' 'some [text]' '--help' '')
run_case 'Bash string array interpolation preserves argument boundaries' 0 \
  $'SYM_ARRAY(ARRAY(TYPE_STRING, ["hello, world", "some [text]", "--help", ""]))\n' '' \
  -p --arr values s "${#values[@]}" "${values[@]}" "$TMP_DIR/values.lisp"

printf '\nTesting original arguments, raw arguments and overrides…\n'
run_case 'argv preserves original grouped option' 0 $'SYM_VARIABLE(TYPE_STRING("-ps"))\n' '' \
  -ps "$TMP_DIR/argv-one.lisp"
run_case 'argc includes executable, options and separator' 0 $'SYM_VARIABLE(TYPE_U32(6))\n' '' \
  -p "$TMP_DIR/argc.lisp" -- --help 'two words'
run_case 'raw args preserve punctuation and spaces' 0 \
  $'SYM_ARRAY(ARRAY(TYPE_STRING, ["--help", "two words", ""]))\n' '' \
  -p "$TMP_DIR/args.lisp" -- --help 'two words' ''
run_case 'args is empty when -- is absent' 0 $'SYM_ARRAY(ARRAY(TYPE_STRING, []))\n' '' \
  -p "$TMP_DIR/args.lisp"
run_case 'raw arguments are not interpreted as source/options' 0 \
  $'SYM_ARRAY(ARRAY(TYPE_STRING, ["--var", "x", "u32:5"]))\n' '' \
  -p "$TMP_DIR/args.lisp" -- --var x u32:5
run_case 'CLI definition overrides argc' 0 $'SYM_VARIABLE(TYPE_U32(42))\n' '' \
  -p --var argc u32:42 "$TMP_DIR/argc.lisp"
run_case 'CLI array overrides args' 0 $'SYM_ARRAY(ARRAY(TYPE_STRING, ["own"]))\n' '' \
  -p --arr args s 1 own "$TMP_DIR/args.lisp" -- ignored

printf '\nTesting CLI failures…\n'
run_case 'unknown long option' 1 '' 'unknown option' --no-such-option
run_case 'invalid grouped short options' 1 '' 'must be last' -sp "$TMP_DIR/stdin.lisp"
run_case 'missing grouped source' 1 '' 'requires a file path' -ps
run_case 'duplicate source' 1 '' 'source file specified more than once' \
  "$TMP_DIR/stdin.lisp" "$TMP_DIR/answer.lisp"
run_case 'missing input file' 1 '' 'No such file or directory' \
  -s "$TMP_DIR/no-such-file.lisp"
run_case 'invalid unsigned scalar' 1 '' 'invalid or out-of-range numeric value' \
  --var answer u32:-1 "$TMP_DIR/answer.lisp"
run_case 'duplicate scalar definition' 1 '' 'specified more than once' \
  --var answer u32:1 --var answer u32:2 "$TMP_DIR/answer.lisp"
run_case 'duplicate array and scalar definition' 1 '' 'specified more than once' \
  --arr-new numbers u32 2 --var numbers u32:1 "$TMP_DIR/numbers.lisp"
run_case 'missing dense array values' 1 '' 'fewer elements' \
  --arr numbers u32 4 1 2
run_case 'bad array length' 1 '' 'invalid length' \
  --arr-new numbers u32 -1 "$TMP_DIR/numbers.lisp"
run_case 'bad dense numeric element' 1 '' 'invalid or out-of-range element at index 1' \
  --arr numbers i32 2 1 1.5 "$TMP_DIR/numbers.lisp"
run_case 'bad sparse numeric element' 1 '' 'invalid element value' \
  --arr-new numbers u32 1 --arr-set numbers 0 -1 "$TMP_DIR/numbers.lisp"
run_case 'sparse assignment before definition' 1 '' 'not previously defined' \
  --arr-set numbers 0 1 --arr-new numbers u32 2 "$TMP_DIR/numbers.lisp"
run_case 'sparse assignment out of bounds' 1 '' 'out of bounds' \
  --arr-new numbers u32 2 --arr-set numbers 2 1 "$TMP_DIR/numbers.lisp"
run_case 'freeze before definition' 1 '' 'cannot freeze undefined' \
  --freeze answer --var answer u32:1 "$TMP_DIR/answer.lisp"
run_case 'cannot modify array after --freeze' 1 '' 'is frozen' \
  --arr-new numbers u32 2 --freeze numbers --arr-set numbers 0 1 \
  "$TMP_DIR/numbers.lisp"

if (( failed != 0 )); then
  PASS_HL=$O_SECONDARY_COLOR
  FAIL_HL=$O_FAIL_COLOR
else
  PASS_HL=$O_PASS_COLOR
  FAIL_HL=$O_SECONDARY_COLOR
fi

printf '\nCLI executable tests: %sPassed: %d%s, %sFailed: %d%s\n' \
  "$PASS_HL" "$passed" "$O_RESET_COLOR" \
  "$FAIL_HL" "$failed" "$O_RESET_COLOR"

# Final exit code
(( passed > 0 && failed == 0 ))
