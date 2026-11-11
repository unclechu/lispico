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
>/dev/null type head
>/dev/null type tail
>/dev/null type ln

cleanup() {
  rm -rfv -- "$TMP_DIR"
}

TMP_DIR="$(mktemp -d)"
trap cleanup EXIT

TIMEOUT=30s

cat tests/examples.env
source tests/examples.env
echo

# Include embedded NUL bytes, non-ASCII bytes, and several 4 KiB chunks.
# The data is random but the test only compares the bytes, not their values.
(set -o xtrace; dd if=/dev/urandom of="$TMP_DIR/input" bs=4096 count=5 status=none)

# A pipe has no advance file size, so (read-file) must grow its buffer.
(set -o xtrace; cat -- "$TMP_DIR/input" | timeout -- "$TIMEOUT" \
  "$LISPICO_EXECUTABLE" -ps tests/file-io/from-stdin.lisp -- "$TMP_DIR/from-stdin" \
  >"$TMP_DIR/stream.stdout" 2>"$TMP_DIR/stream.stderr")

(set -o xtrace; cmp -- "$TMP_DIR/input" "$TMP_DIR/from-stdin")
capture=$(<"$TMP_DIR/stream.stdout")
if [[ $capture != SYM_EMPTY || -s "$TMP_DIR/stream.stderr" ]]; then
  >&2 echo '[FAIL] File I/O stream test produced unexpected output'
  >&2 cat -- "$TMP_DIR/stream.stderr"
  exit 1
fi

# Repeat through a regular file, where fstat() supplies a size hint.
(set -o xtrace; timeout -- "$TIMEOUT" \
  "$LISPICO_EXECUTABLE" -ps tests/file-io/from-file.lisp -- \
  "$TMP_DIR/input" "$TMP_DIR/from-file" \
  >"$TMP_DIR/file.stdout" 2>"$TMP_DIR/file.stderr")

(set -o xtrace; cmp -- "$TMP_DIR/input" "$TMP_DIR/from-file")
capture=$(<"$TMP_DIR/file.stdout")
if [[ $capture != SYM_EMPTY || -s "$TMP_DIR/file.stderr" ]]; then
  >&2 echo '[FAIL] File I/O regular file test produced unexpected output'
  >&2 cat -- "$TMP_DIR/file.stderr"
  exit 1
fi

# Empty streams still produce a valid, NUL-terminated zero-length string.
(set -o xtrace; printf '' | timeout -- "$TIMEOUT" \
  "$LISPICO_EXECUTABLE" -ps tests/file-io/from-stdin.lisp -- "$TMP_DIR/empty" \
  >"$TMP_DIR/empty.stdout" 2>"$TMP_DIR/empty.stderr")

if [[ -s "$TMP_DIR/empty" || -s "$TMP_DIR/empty.stderr" ]]; then
  >&2 echo '[FAIL] Empty file I/O stream test failed'
  exit 1
fi

# A bounded stdin read must leave the rest of the pipe available to a later
# unlimited read, including the one-byte lookahead used to detect truncation.
(set -o xtrace; cat -- "$TMP_DIR/input" | timeout -- "$TIMEOUT" \
  "$LISPICO_EXECUTABLE" -ps tests/file-io/from-stdin-bounded.lisp -- \
  "$TMP_DIR/prefix" "$TMP_DIR/suffix" \
  >"$TMP_DIR/bounded.stdout" 2>"$TMP_DIR/bounded.stderr")

(set -o xtrace; head -c 4096 -- "$TMP_DIR/input" | cmp -- - "$TMP_DIR/prefix")
(set -o xtrace; tail -c +4097 -- "$TMP_DIR/input" | cmp -- - "$TMP_DIR/suffix")
capture=$(<"$TMP_DIR/bounded.stdout")
if [[ $capture != SYM_EMPTY || -s "$TMP_DIR/bounded.stderr" ]]; then
  >&2 echo '[FAIL] File I/O bounded stdin test produced unexpected output'
  >&2 cat -- "$TMP_DIR/bounded.stderr"
  exit 1
fi

# Build temporary symbolic links to test the classifications returned by (file-type).
# Relative targets verify that stat() resolves links relative to their directory.
(
  set -o xtrace
  ln -s input "$TMP_DIR/link-to-file"
  ln -s . "$TMP_DIR/link-to-dir"
  ln -s missing-target "$TMP_DIR/link-dead"
  ln -s /dev/null "$TMP_DIR/link-to-device"
)

(set -o xtrace; timeout -- "$TIMEOUT" \
  "$LISPICO_EXECUTABLE" -ps tests/file-io/file-type.lisp -- \
  "$TMP_DIR/input" "$TMP_DIR" \
  "$TMP_DIR/link-to-file" "$TMP_DIR/link-to-dir" \
  "$TMP_DIR/link-dead" "$TMP_DIR/link-to-device" \
  "$TMP_DIR/missing-target" \
  >"$TMP_DIR/type.stdout" 2>"$TMP_DIR/type.stderr")

capture=$(<"$TMP_DIR/type.stdout")
if [[ $capture != SYM_EMPTY || -s "$TMP_DIR/type.stderr" ]]; then
  >&2 echo '[FAIL] File type classification test produced unexpected output'
  >&2 cat -- "$TMP_DIR/type.stderr"
  exit 1
fi

# Following a symbolic link loop must fail, not return the dead-link type.
(set -o xtrace; ln -s link-loop "$TMP_DIR/link-loop")
if (set -o xtrace; timeout -- "$TIMEOUT" \
  "$LISPICO_EXECUTABLE" -ps tests/file-io/file-type-error.lisp -- "$TMP_DIR/link-loop" \
  >"$TMP_DIR/type-loop.stdout" 2>"$TMP_DIR/type-loop.stderr"); then
  >&2 echo '[FAIL] File type symbolic link loop should fail evaluation'
  exit 1
fi
capture=$(<"$TMP_DIR/type-loop.stderr")
if [[ $capture != *'(file-type) unable to follow symbolic link'* ]]; then
  >&2 echo '[FAIL] File type symbolic link loop produced an unexpected error'
  >&2 cat -- "$TMP_DIR/type-loop.stderr"
  exit 1
fi

echo '[PASS] File I/O streaming and file type tests passed'
