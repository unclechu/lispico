// POSIX interfaces for checking the size of regular files without seeking.
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "stdlib-file-io.h"
#include "stdlib-validation-utils.h"
#include "symbol-print.h"
#include "evaluate.h"
#include "wipe-memory.h"

#define FILE_IO_CHUNK_SIZE 4096 /* 4 KiB */

// Validate a filename as a nonempty, NUL-terminated Lispico string.
// Embedded NUL bytes cannot be passed to the C file APIs as pathnames.
static bool validate_file_name(
  const Datum *arg,
  const char *fn_name,
  char (*fail_msg)[ERR_MSG_SIZE]
) {
  if (arg->kind != SYM_VARIABLE) {
    snprintf(
      *fail_msg,
      ERR_MSG_SIZE,
      "(%s) expects a string filename, got kind: %s",
      fn_name,
      show_SymbolKind(arg->kind)
    );
    return false;
  }

  if (arg->as.value.type != TYPE_STRING) {
    snprintf(
      *fail_msg,
      ERR_MSG_SIZE,
      "(%s) expects a string filename, got type: %s",
      fn_name,
      show_ValueType(arg->as.value.type)
    );
    return false;
  }

  String string = arg->as.value.as.string;
  if (
    string.length == 0 ||
    string.data == NULL ||
    memchr(string.data, '\0', string.length) != NULL ||
    string.data[string.length] != '\0'
  ) {
    snprintf(*fail_msg, ERR_MSG_SIZE, "(%s) invalid filename", fn_name);
    return false;
  }

  return true;
}

// (read-file) — Read a complete file or stream as a binary-safe string.
Datum std_read_file(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "read-file";
  FILE *file = NULL;
  bool borrowed_stream = false;
  char *data = NULL;
  size_t capacity = 0;
  size_t length = 0;

  if (!validate_argc(interpreter, fn_name, &fail_msg, 1, argc)) return return_value;
  if (!validate_file_name(&args[0], fn_name, &fail_msg)) goto fail;

  const char *filename = args[0].as.value.as.string.data;
  // Use the existing stdin stream, even on hosts without a `/dev/stdin` node.
  // Do not close the host’s standard stream when this function finishes.
  if (strcmp(filename, "/dev/stdin") == 0) {
    file = stdin;
    borrowed_stream = true;
  } else {
    file = fopen(filename, "rb");
  }
  if (file == NULL) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) unable to open “%s” for reading: %s",
      fn_name,
      filename,
      strerror(errno)
    );
    goto fail;
  }

  // Always reserve an extra byte for a trailing `\0`. Start with a small
  // buffer for pipes, terminals, `/dev/stdin`, and other unseekable streams.
  capacity = FILE_IO_CHUNK_SIZE + 1;

  // For a regular file, use its current size as an initial allocation hint.
  // The file may grow or shrink while being read, so the streaming loop below
  // remains authoritative. `fstat()` does not consume bytes or seek the stream.
  struct stat metadata;
  if (fstat(fileno(file), &metadata) == 0 && S_ISREG(metadata.st_mode)) {
    if (metadata.st_size < 0 || (uintmax_t)metadata.st_size >= SIZE_MAX) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) file size is too large", fn_name);
      goto fail;
    }

    size_t wanted_capacity = (size_t)metadata.st_size + 1;
    if (wanted_capacity > capacity) capacity = wanted_capacity;
  }

  data = malloc(capacity);
  if (data == NULL) goto allocation_failed;

  for (;;) {
    // The last byte is reserved for the terminator, not file contents.
    // Check for EOF before growing a full buffer. A regular file whose size
    // matched the initial hint should not needlessly double its allocation.
    if (length == capacity - 1) {
      int next_byte = fgetc(file);
      if (next_byte == EOF) {
        if (ferror(file)) goto read_failed;
        break;
      }

      if (capacity > SIZE_MAX / 2) {
        snprintf(fail_msg, sizeof(fail_msg), "(%s) file contents are too large", fn_name);
        goto fail;
      }

      size_t new_capacity = capacity * 2;
      char *grown = realloc(data, new_capacity);
      if (grown == NULL) goto allocation_failed;

      data = grown;
      capacity = new_capacity;

      // Preserve the lookahead byte that established the need to grow.
      data[length] = (char)(unsigned char)next_byte;
      length++;
    }

    size_t available = capacity - length - 1;
    size_t chunk_size = available > FILE_IO_CHUNK_SIZE ? FILE_IO_CHUNK_SIZE : available;
    size_t read_count = fread(data + length, 1, chunk_size, file);
    length += read_count;

    if (read_count != chunk_size) {
      if (ferror(file)) goto read_failed;
      if (feof(file)) break;
    }
  }

  data[length] = '\0';

  // Close only streams opened by this function. stdin belongs to the host.
  // A failed `fclose()` must not silently turn a failed read into success.
  int close_result = borrowed_stream ? 0 : fclose(file);
  file = NULL;
  if (close_result != 0) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) unable to close “%s” after reading: %s",
      fn_name,
      filename,
      strerror(errno)
    );
    goto fail;
  }

  // Keep only the bytes that will be returned, plus their terminator.
  // Failure to shrink is harmless; the existing allocation remains valid.
  if (capacity != length + 1) {
    char *shrunk = realloc(data, length + 1);
    if (shrunk != NULL) {
      data = shrunk;
      capacity = length + 1;
    }
  }

  // The final value must outlive this function. Give the grown buffer directly
  // to the evaluator rather than allocating and copying the entire file again.
  if (!adopt_runtime_data(interpreter, data, capacity)) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) runtime allocation capacity reached",
      fn_name
    );
    goto fail;
  }

  return_value = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = {
      .type = TYPE_STRING,
      .as.string = { .data = data, .length = length },
    },
  };

  return return_value;

  allocation_failed:
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) file buffer allocation failed (OOM or size limit reached)",
      fn_name
    );
    goto fail;

  read_failed:
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) unable to read “%s”: %s",
      fn_name,
      filename,
      strerror(errno)
    );
    goto fail;

  fail:
    if (file != NULL && !borrowed_stream) fclose(file);
    if (data != NULL) {
      // The evaluator owns the buffer only after `adopt_runtime_data` succeeds.
      wipe_memory(data, capacity);
      free(data);
    }
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (write-file) — Truncate/create a file and write a string in small chunks.
Datum std_write_file(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "write-file";
  FILE *file = NULL;
  bool borrowed_stream = false;

  if (!validate_argc(interpreter, fn_name, &fail_msg, 2, argc)) return return_value;
  if (!validate_file_name(&args[0], fn_name, &fail_msg)) goto fail;

  if (args[1].kind != SYM_VARIABLE) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) expects a string content value, got kind: %s",
      fn_name,
      show_SymbolKind(args[1].kind)
    );
    goto fail;
  }
  if (args[1].as.value.type != TYPE_STRING) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) expects a string content value, got type: %s",
      fn_name,
      show_ValueType(args[1].as.value.type)
    );
    goto fail;
  }

  String content = args[1].as.value.as.string;
  if (content.length != 0 && content.data == NULL) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid string content", fn_name);
    goto fail;
  }

  const char *filename = args[0].as.value.as.string.data;
  // Directly use the standard output streams for `/dev/stdout` and `/dev/stderr`.
  // This also works on hosts that do not expose these as filesystem paths.
  if (strcmp(filename, "/dev/stdout") == 0) {
    file = stdout;
    borrowed_stream = true;
  } else if (strcmp(filename, "/dev/stderr") == 0) {
    file = stderr;
    borrowed_stream = true;
  } else {
    file = fopen(filename, "wb");
  }
  if (file == NULL) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) unable to open “%s” for writing: %s",
      fn_name,
      filename,
      strerror(errno)
    );
    goto fail;
  }

  // `fwrite()` receives only small chunks even if the Lispico string is large.
  // A short write is an I/O failure; do not return success for partial files.
  for (size_t offset = 0; offset < content.length;) {
    size_t remaining = content.length - offset;
    size_t chunk_size = remaining > FILE_IO_CHUNK_SIZE ? FILE_IO_CHUNK_SIZE : remaining;
    size_t written = fwrite(content.data + offset, 1, chunk_size, file);
    if (written != chunk_size) goto write_failed;
    offset += written;
  }

  // Flush host-owned streams, and close files opened here. Both operations
  // can detect delayed write failures in the C library's output buffer.
  int close_result = borrowed_stream ? fflush(file) : fclose(file);
  file = NULL;
  if (close_result != 0) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) unable to flush or close “%s” after writing: %s",
      fn_name,
      filename,
      strerror(errno)
    );
    goto fail;
  }

  return return_value;

  write_failed:
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) unable to write “%s”: %s",
      fn_name,
      filename,
      strerror(errno)
    );
    goto fail;

  fail:
    if (file != NULL && !borrowed_stream) fclose(file);
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}
