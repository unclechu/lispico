#!/usr/bin/env bash
#
# Test the optional bat preset and the complete shell source renderer.
#
# Allow unicode quotes:
# shellcheck disable=SC1111
# Do not assume environment variables presence:
# shellcheck enable=check-unassigned-uppercase
# Set directory to project root:
# shellcheck source-path=SCRIPTDIR/..
#
set -o errexit || exit; set -o errtrace; set -o nounset; set -o pipefail

SCRIPT_DIR=$(dirname -- "${BASH_SOURCE[0]}")
cd -- "$SCRIPT_DIR/.." # Switch to project root

: "${LISPICO_EXECUTABLE:=./lispico}"

# Guard dependencies
if [[ ! -x "$LISPICO_EXECUTABLE" ]]; then
  >&2 printf "“%s” is not an executable (did you run “./build.sh” first?)\n" "$LISPICO_EXECUTABLE"
  exit 1
fi
>/dev/null type mktemp
>/dev/null type rm
>/dev/null type mkdir
>/dev/null type cat
>/dev/null type awk
>/dev/null type chmod
>/dev/null type cmp
>/dev/null type diff
>/dev/null type timeout
>/dev/null type env
>/dev/null type /usr/bin/env
>/dev/null type bash
>/dev/null type grep
>/dev/null type sed

source tests/dev-scripts/colors.sh

TMP_DIR=$(mktemp -d)
cleanup() { rm -rf -- "$TMP_DIR"; }
trap cleanup EXIT

mkdir -p -- "$TMP_DIR/bin"
printf '\t10\n20\n\t(+ 1 missing)\n' > "$TMP_DIR/failure.lisp"

# An independent fake bat makes the tests deterministic on machines with or
# without a real bat executable. It can also simulate invalid output/failure.
cat > "$TMP_DIR/bin/bat" <<'BAT'
#!/usr/bin/env bash
set -o errexit || exit; set -o errtrace; set -o nounset; set -o pipefail
printf '%s\n' "$@" > "$LISPICO_TEST_BAT_ARGS"
cat > "$LISPICO_TEST_BAT_STDIN"
case "${LISPICO_TEST_BAT_RESULT:-ok}" in
  failed)
    >&2 printf 'Fake bat diagnostic that must be suppressed\n'
    exit 23
    ;;
  invalid)
    printf 'Not the original source\n'
    exit 0
    ;;
  ok)
    first=1
    last=1
    for arg in "$@"; do
      if [[ $arg == --line-range=* ]]; then
        range=${arg#--line-range=}
        first=${range%%:*}
        last=${range#*:}
      fi
    done
    awk -v first="$first" -v last="$last" \
      'NR >= first && NR <= last { printf "\033[35m%s\033[0m\n", $0 }' \
      "$LISPICO_TEST_BAT_STDIN"
    ;;
  *) exit 24 ;;
esac
BAT
chmod +x -- "$TMP_DIR/bin/bat"

passed=0
failed=0

# Compare all output bytes, including ANSI color codes and stderr warnings.
check_result() {
  local name=$1
  local expected_status=$2
  local expected_stdout=$3
  local expected_stderr=$4
  shift 4

  local status=0
  if timeout -- 15s "$LISPICO_EXECUTABLE" "$@" >"$TMP_DIR/actual.stdout" 2>"$TMP_DIR/actual.stderr"
  then
    status=0
  else
    status=$?
  fi

  printf '%s' "$expected_stdout" >"$TMP_DIR/expected.stdout"
  printf '%s' "$expected_stderr" >"$TMP_DIR/expected.stderr"

  if (( status == expected_status )) \
  && cmp -s -- "$TMP_DIR/expected.stdout" "$TMP_DIR/actual.stdout" \
  && cmp -s -- "$TMP_DIR/expected.stderr" "$TMP_DIR/actual.stderr"
  then
    printf '%s[PASS]%s %s\n' "$O_PASS_COLOR" "$O_RESET_COLOR" "$name"
    (( passed += 1 ))
  else
    >&2 printf '%s[FAIL]%s %s (expected exit %d, got %d)\n' \
      "$E_FAIL_COLOR" "$E_RESET_COLOR" "$name" "$expected_status" "$status"
    >&2 diff -u -- "$TMP_DIR/expected.stdout" "$TMP_DIR/actual.stdout" || true
    >&2 diff -u -- "$TMP_DIR/expected.stderr" "$TMP_DIR/actual.stderr" || true
    (( failed += 1 ))
  fi
}

export PATH="$TMP_DIR/bin:$PATH"
export LISPICO_TEST_BAT_ARGS="$TMP_DIR/bat-args"
export LISPICO_TEST_BAT_STDIN="$TMP_DIR/bat-stdin"

header=$'[FAIL] Evaluation failed: Line 3 Column 7: Undefined variable\n'
plain=$'2 | 20\n3 |     (+ 1 missing)\n  |          ↑\n'
colored=$'2 | \033[35m20\033[0m\033[0m\n3 | \033[35m    (+ 1 missing)\033[0m\033[0m\n  |          ↑\n'

printf 'Testing built-in bat preset…\n'
unset LISPICO_SOURCE_HIGHLIGHT LISPICO_SOURCE_HIGHLIGHT_SH || true
unset LISPICO_TEST_BAT_RESULT || true
check_result 'bat is the default renderer' 1 '' "${header}${colored}" \
  --source-context-before=1 "$TMP_DIR/failure.lisp"

printf '\t10\n20\n\t(+ 1 missing)\n' | sed $'s/\t/    /g' > "$TMP_DIR/expected-bat-stdin"
if grep -Fqx -- '--style=plain' "$TMP_DIR/bat-args" \
&& grep -Fqx -- '--color=always' "$TMP_DIR/bat-args" \
&& grep -Fqx -- '--line-range=2:3' "$TMP_DIR/bat-args" \
&& cmp -s -- "$TMP_DIR/expected-bat-stdin" "$TMP_DIR/bat-stdin"
then
  printf \
    '%s[PASS]%s bat received the full source with tabs expanded and correct options\n' \
    "$O_PASS_COLOR" "$O_RESET_COLOR"
  (( passed += 1 ))
else
  >&2 printf '%s[FAIL]%s bat input or arguments differ\n' "$E_FAIL_COLOR" "$E_RESET_COLOR"
  (( failed += 1 ))
fi

export LISPICO_TEST_BAT_RESULT=failed
check_result 'bat command failure silently falls back' 1 '' "${header}${plain}" \
  --source-highlight=bat --source-context-before=1 "$TMP_DIR/failure.lisp"
export LISPICO_TEST_BAT_RESULT=invalid
check_result 'invalid bat output silently falls back' 1 '' "${header}${plain}" \
  --source-highlight=bat --source-context-before=1 "$TMP_DIR/failure.lisp"
unset LISPICO_TEST_BAT_RESULT

rm -f -- "$TMP_DIR/bat-args"
check_result 'plain renderer does not invoke bat' 1 '' "${header}${plain}" \
  --source-highlight=none --source-context-before=1 "$TMP_DIR/failure.lisp"
if [[ ! -e $TMP_DIR/bat-args ]]; then
  printf '%s[PASS]%s none mode omitted bat invocation\n' "$O_PASS_COLOR" "$O_RESET_COLOR"
  (( passed += 1 ))
else
  >&2 printf '%s[FAIL]%s none mode invoked bat\n' "$E_FAIL_COLOR" "$E_RESET_COLOR"
  (( failed += 1 ))
fi

printf '\nTesting custom shell renderer…\n'
# shellcheck disable=SC2016
shell_metadata='printf "context=%s,%s,%s,%s,%s\\n" "$LISPICO_SOURCE_CONTEXT_BEFORE" "$LISPICO_SOURCE_CONTEXT_FIRST_LINE" "$LISPICO_SOURCE_CONTEXT_LINE" "$LISPICO_SOURCE_CONTEXT_COLUMN" "$LISPICO_SOURCE_CONTEXT_BYTE_OFFSET"; cat'
check_result 'shell receives metadata and unmodified source on stdin' 1 '' \
  "${header}"$'context=1,2,3,7,13\n\t10\n20\n\t(+ 1 missing)\n' \
  --source-highlight=sh "--source-highlight-sh=$shell_metadata" \
  --source-context-before=1 "$TMP_DIR/failure.lisp"

shell_error='printf "shell stdout\\n"; printf "shell stderr\\n" >&2; exit 7'
check_result 'shell retains stderr and reports command exit status' 1 '' \
  "${header}"$'shell stdout\nshell stderr\n[WARN] Source highlighter command exited with status 7\n' \
  --source-highlight=sh "--source-highlight-sh=$shell_error" "$TMP_DIR/failure.lisp"

export LISPICO_SOURCE_HIGHLIGHT=sh
export LISPICO_SOURCE_HIGHLIGHT_SH='printf "selected environment command\\n"'
check_result 'environment can configure shell renderer' 1 '' \
  "${header}"$'selected environment command\n' \
  "$TMP_DIR/failure.lisp"
check_result 'CLI mode overrides environment mode' 1 '' \
  "${header}"$'1 |     10\n2 | 20\n3 |     (+ 1 missing)\n  |          ↑\n' \
  --source-highlight=none "$TMP_DIR/failure.lisp"
check_result 'CLI shell command overrides environment command' 1 '' \
  "${header}"$'selected CLI command\n' \
  --source-highlight=sh '--source-highlight-sh=printf "selected CLI command\n"' \
  "$TMP_DIR/failure.lisp"

unset LISPICO_SOURCE_HIGHLIGHT_SH
check_result 'sh without command reports a configuration error' 1 '' \
  $'[FAIL] Source highlighter sh mode requires a shell command\n' \
  "$TMP_DIR/failure.lisp"
check_result 'source-map disabling ignores highlighting configuration' 1 '' \
  $'[FAIL] Evaluation failed: Expression Index 5: Undefined variable\n' \
  --no-source-map "$TMP_DIR/failure.lisp"

if (( failed != 0 )); then
  PASS_HL=$O_SECONDARY_COLOR
  FAIL_HL=$O_FAIL_COLOR
else
  PASS_HL=$O_PASS_COLOR
  FAIL_HL=$O_SECONDARY_COLOR
fi

if (( failed == 0 )); then
  printf '\nSource highlighting tests: %s%d passed%s\n' "$PASS_HL" "$passed" "$O_RESET_COLOR"
else
  >&2 printf '\nSource highlighting tests: %s%d passed%s, %s%d failed%s\n' \
    "$PASS_HL" "$passed" "$O_RESET_COLOR" \
    "$FAIL_HL" "$failed" "$O_RESET_COLOR"
  exit 1
fi
