#!/usr/bin/env bash
#
# Exercise file I/O on regular files and unseekable streams with binary data.
#
# Allow unicode quotes:
# shellcheck disable=SC1111
# Do not assume environment variables presence:
# shellcheck enable=check-unassigned-uppercase
#
# Set directory to project root:
# shellcheck source-path=SCRIPTDIR/..

set -o errexit || exit; set -o errtrace; set -o nounset; set -o pipefail

SCRIPT_DIR=$(dirname -- "${BASH_SOURCE[0]}")
cd -- "$SCRIPT_DIR/.." # Switch to project root

: "${LISPICO_EXECUTABLE:=./lispico}"

# Guard dependencies
if [[ ! -x "$LISPICO_EXECUTABLE" ]]; then
  >&2 printf "“%s” is not an executable (did you run “./build.sh” first?)\n" "$LISPICO_EXECUTABLE"
  exit 1
fi
>/dev/null type dd
>/dev/null type mktemp
>/dev/null type cat
>/dev/null type timeout
>/dev/null type cmp

cleanup() {
  rm -rfv -- "$TMP_DIR"
}

TMP_DIR="$(mktemp -d)"
trap cleanup EXIT

cat tests/examples.env
source tests/examples.env
echo

# Include embedded NUL bytes, non-ASCII bytes, and several 4 KiB chunks.
# The data is random but the test only compares the bytes, not their values.
dd if=/dev/urandom of="$TMP_DIR/input" bs=4096 count=5 status=none

# A pipe has no advance file size, so (read-file) must grow its buffer.
cat -- "$TMP_DIR/input" | timeout -- 30s \
  "$LISPICO_EXECUTABLE" tests/file-io-from-stdin.lisp "$TMP_DIR/from-stdin" \
  >"$TMP_DIR/stream.stdout" 2>"$TMP_DIR/stream.stderr"

cmp -- "$TMP_DIR/input" "$TMP_DIR/from-stdin"
capture=$(<"$TMP_DIR/stream.stdout")
if [[ $capture != SYM_EMPTY || -s "$TMP_DIR/stream.stderr" ]]; then
  >&2 echo '[FAIL] File I/O stream test produced unexpected output'
  >&2 cat -- "$TMP_DIR/stream.stderr"
  exit 1
fi

# Repeat through a regular file, where fstat() supplies a size hint.
timeout -- 30s \
  "$LISPICO_EXECUTABLE" tests/file-io-from-file.lisp \
  "$TMP_DIR/input" "$TMP_DIR/from-file" \
  >"$TMP_DIR/file.stdout" 2>"$TMP_DIR/file.stderr"

cmp -- "$TMP_DIR/input" "$TMP_DIR/from-file"
capture=$(<"$TMP_DIR/file.stdout")
if [[ $capture != SYM_EMPTY || -s "$TMP_DIR/file.stderr" ]]; then
  >&2 echo '[FAIL] File I/O regular file test produced unexpected output'
  >&2 cat -- "$TMP_DIR/file.stderr"
  exit 1
fi

# Empty streams still produce a valid, NUL-terminated zero-length string.
printf '' | timeout -- 30s \
  "$LISPICO_EXECUTABLE" tests/file-io-from-stdin.lisp "$TMP_DIR/empty" \
  >"$TMP_DIR/empty.stdout" 2>"$TMP_DIR/empty.stderr"

if [[ -s "$TMP_DIR/empty" || -s "$TMP_DIR/empty.stderr" ]]; then
  >&2 echo '[FAIL] Empty file I/O stream test failed'
  exit 1
fi

echo '[PASS] File I/O streaming tests passed'
