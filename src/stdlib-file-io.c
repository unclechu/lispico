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
#include "symbol-error.h"
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

// (file-exists) — Check whether a filesystem path exists (including directories).
Datum std_file_exists(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "file-exists";

  if (!validate_argc(interpreter, fn_name, &fail_msg, 1, argc)) return return_value;
  if (!validate_file_name(&args[0], fn_name, &fail_msg)) goto fail;

  // stat() follows symbolic links and succeeds for files and directories.
  // A failure, including a missing or inaccessible path, is reported as false.
  struct stat metadata;
  bool exists = stat(args[0].as.value.as.string.data, &metadata) == 0;

  return_value = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = { .type = TYPE_U32, .as.u32 = exists ? 1 : 0 },
  };

  return return_value;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// Types of existing filesystem objects, including the targets of symbolic links
typedef enum {
  FILE_TYPE_OTHER = 0,
  FILE_TYPE_DOES_NOT_EXIST = 1,
  FILE_TYPE_FILE = 2,
  FILE_TYPE_DIR = 3,
  FILE_TYPE_SYMLINK_DEAD = 4,
  FILE_TYPE_SYMLINK_FILE = 5,
  FILE_TYPE_SYMLINK_DIR = 6,
  FILE_TYPE_SYMLINK_OTHER = 7,
} FileType;

typedef struct {
  const char *var_name;
  const uint32_t type_code;
} FileTypeConstant;

static const FileTypeConstant file_type_constants[] = {
  { .var_name = "file-type-other", .type_code = FILE_TYPE_OTHER },
  { .var_name = "file-type-does-not-exist", .type_code = FILE_TYPE_DOES_NOT_EXIST },
  { .var_name = "file-type-file", .type_code = FILE_TYPE_FILE },
  { .var_name = "file-type-dir", .type_code = FILE_TYPE_DIR },
  { .var_name = "file-type-symlink-dead", .type_code = FILE_TYPE_SYMLINK_DEAD },
  { .var_name = "file-type-symlink-file", .type_code = FILE_TYPE_SYMLINK_FILE },
  { .var_name = "file-type-symlink-dir", .type_code = FILE_TYPE_SYMLINK_DIR },
  { .var_name = "file-type-symlink-other", .type_code = FILE_TYPE_SYMLINK_OTHER },
};

#define FILE_TYPE_CONSTANTS_AMOUNT \
  (sizeof(file_type_constants) / sizeof(file_type_constants[0]))

// Provide file type constants-ish variables if they are not defined yet.
// They are created lazily only after a successful (file-type) lookup.
// Existing user-defined symbols are left untouched, as with read-file statuses.
static bool provide_file_type_constants(Interpreter *interpreter, char (*fail_msg)[ERR_MSG_SIZE]) {
  for (size_t i = 0; i < FILE_TYPE_CONSTANTS_AMOUNT; ++i) {
    const char *var_name = file_type_constants[i].var_name;
    const uint32_t type_code = file_type_constants[i].type_code;
    Symbol *symbol = symbol_lookup(interpreter, var_name);
    if (symbol == NULL) {
      symbol = symbol_define(interpreter, var_name, SYM_VARIABLE);
      if (symbol == NULL) {
        extract_error_message_from_symbol_define_error(interpreter, fail_msg, var_name);
        return false;
      }
      symbol->as.variable = (Value) { .type = TYPE_U32, .as.u32 = type_code };
    }
  }

  return true;
}

// (file-type) — Identify a filesystem object, preserving symbolic link information.
Datum std_file_type(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "file-type";
  FileType file_type = FILE_TYPE_OTHER;

  if (!validate_argc(interpreter, fn_name, &fail_msg, 1, argc)) return return_value;
  if (!validate_file_name(&args[0], fn_name, &fail_msg)) goto fail;

  const char *filename = args[0].as.value.as.string.data;
  struct stat metadata;

  // `lstat()` inspects the path itself instead of following symbolic links.
  // Missing paths are ordinary results, but unexpected filesystem errors fail.
  if (lstat(filename, &metadata) != 0) {
    if (errno == ENOENT || errno == ENOTDIR) {
      file_type = FILE_TYPE_DOES_NOT_EXIST;
    } else {
      snprintf(
        fail_msg,
        sizeof(fail_msg),
        "(%s) unable to inspect “%s”: %s",
        fn_name,
        filename,
        strerror(errno)
      );
      goto fail;
    }
  } else if (S_ISLNK(metadata.st_mode)) {
    // Follow the symbolic link only after identifying it with `lstat()`.
    // A missing target is a dead link; lack of access is an evaluation failure.
    if (stat(filename, &metadata) != 0) {
      if (errno == ENOENT || errno == ENOTDIR) {
        file_type = FILE_TYPE_SYMLINK_DEAD;
      } else {
        snprintf(
          fail_msg,
          sizeof(fail_msg),
          "(%s) unable to follow symbolic link “%s”: %s",
          fn_name,
          filename,
          strerror(errno)
        );
        goto fail;
      }
    } else if (S_ISREG(metadata.st_mode)) {
      file_type = FILE_TYPE_SYMLINK_FILE;
    } else if (S_ISDIR(metadata.st_mode)) {
      file_type = FILE_TYPE_SYMLINK_DIR;
    } else {
      // Symbolic links to devices, sockets, FIFOs, and other special objects.
      file_type = FILE_TYPE_SYMLINK_OTHER;
    }
  } else if (S_ISREG(metadata.st_mode)) {
    file_type = FILE_TYPE_FILE;
  } else if (S_ISDIR(metadata.st_mode)) {
    file_type = FILE_TYPE_DIR;
  } else {
    // Existing devices, sockets, FIFOs, and other non-file/non-directory objects.
    file_type = FILE_TYPE_OTHER;
  }

  if (!provide_file_type_constants(interpreter, &fail_msg)) goto fail;

  return_value = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = { .type = TYPE_U32, .as.u32 = (uint32_t)file_type },
  };

  return return_value;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// For when `MAX_BYTES` argument is set
typedef enum {
  // Reached EOF, the file is fully consumed
  READ_FILE_STATUS_OK = 0,

  // File was truncated early (there is more data that was not consumed beyond the result)
  READ_FILE_STATUS_TRUNCATED = 1,
} ReadFileStatus;

#define READ_FILE_STATUS_VAR_NAME "read-file-status"

typedef struct {
  const char *var_name;
  const uint32_t status_code;
} ReadFileStatusConstant;

static const ReadFileStatusConstant read_file_status_constants[] = {
  { .var_name = "read-file-status-ok", .status_code = READ_FILE_STATUS_OK },
  { .var_name = "read-file-status-truncated", .status_code = READ_FILE_STATUS_TRUNCATED },
};

#define READ_FILE_STATUS_CONSTANTS_AMOUNT \
  (sizeof(read_file_status_constants) / sizeof(read_file_status_constants[0]))

// Provide status constants-ish variables (if they are not set yet)
// and report the result of a successful bounded (read-file) operation.
//
// Status variables are initialized lazily, only when MAX_BYTES is provided.
// Unlimited reads do not create or update these variables.
//
// - `read-file-status-ok`        — u32, READ_FILE_STATUS_OK
// - `read-file-status-truncated` — u32, READ_FILE_STATUS_TRUNCATED
// - `read-file-status`           — u32, status of the last successful bounded read
static bool report_read_file_status(
  Interpreter *interpreter,
  ReadFileStatus reported_status,
  char (*fail_msg)[ERR_MSG_SIZE]
) {
  const char fn_name[] = "read-file";

  // Set constants if they are not already set
  for (size_t i = 0; i < READ_FILE_STATUS_CONSTANTS_AMOUNT; ++i) {
    const char *var_name = read_file_status_constants[i].var_name;
    const uint32_t status_code = read_file_status_constants[i].status_code;
    Symbol *symbol = symbol_lookup(interpreter, var_name);
    if (symbol == NULL) {
      symbol = symbol_define(interpreter, var_name, SYM_VARIABLE);
      if (symbol == NULL) {
        extract_error_message_from_symbol_define_error(interpreter, fail_msg, var_name);
        return false;
      } else {
        symbol->as.variable = (Value) { .type = TYPE_U32, .as.u32 = status_code };
      }
    }
  }

  Symbol *symbol = symbol_lookup(interpreter, READ_FILE_STATUS_VAR_NAME);
  if (symbol != NULL) {
    if (symbol->kind != SYM_VARIABLE) {
      snprintf(
        *fail_msg,
        sizeof(*fail_msg),
        "(%s) expects a u32 variable for “%s” symbol, got kind: %s",
        fn_name,
        READ_FILE_STATUS_VAR_NAME,
        show_SymbolKind(symbol->kind)
      );
      return false;
    }
    if (symbol->as.variable.type != TYPE_U32) {
      snprintf(
        *fail_msg,
        sizeof(*fail_msg),
        "(%s) expects a u32 variable for “%s” symbol, got type: %s",
        fn_name,
        READ_FILE_STATUS_VAR_NAME,
        show_ValueType(symbol->as.variable.type)
      );
      return false;
    }
  } else {
    symbol = symbol_define(interpreter, READ_FILE_STATUS_VAR_NAME, SYM_VARIABLE);
    if (symbol == NULL) {
      extract_error_message_from_symbol_define_error(
        interpreter,
        fail_msg,
        READ_FILE_STATUS_VAR_NAME
      );
      return false;
    }
  }

  symbol->as.variable = (Value) { .type = TYPE_U32, .as.u32 = reported_status };
  return true;
}

// (read-file) — Read a file or a bounded prefix as a binary-safe string.
Datum std_read_file(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "read-file";
  FILE *file = NULL;
  bool borrowed_stream = false;
  bool data_adopted = false;
  char *data = NULL;
  size_t capacity = 0;
  size_t length = 0;
  bool has_max_bytes = argc == 2;
  size_t max_bytes = 0;
  ReadFileStatus read_status = READ_FILE_STATUS_OK;

  if (argc < 1 || argc > 2) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) expects 1 or 2 arguments, got %zu",
      fn_name,
      argc
    );
    goto fail;
  }
  if (!validate_file_name(&args[0], fn_name, &fail_msg)) goto fail;

  if (has_max_bytes) {
    if (args[1].kind != SYM_VARIABLE) {
      snprintf(
        fail_msg,
        sizeof(fail_msg),
        "(%s) expects a u32 MAX_BYTES, got kind: %s",
        fn_name,
        show_SymbolKind(args[1].kind)
      );
      goto fail;
    }
    if (args[1].as.value.type != TYPE_U32) {
      snprintf(
        fail_msg,
        sizeof(fail_msg),
        "(%s) expects a u32 MAX_BYTES, got type: %s",
        fn_name,
        show_ValueType(args[1].as.value.type)
      );
      goto fail;
    }
    max_bytes = (size_t)args[1].as.value.as.u32;
  }

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
  if (has_max_bytes && max_bytes < FILE_IO_CHUNK_SIZE) capacity = max_bytes + 1;

  // For a regular file, use its current size as an initial allocation hint.
  // The file may grow or shrink while being read, so the streaming loop below
  // remains authoritative. `fstat()` does not consume bytes or seek the stream.
  struct stat metadata;
  if (fstat(fileno(file), &metadata) == 0 && S_ISREG(metadata.st_mode)) {
    if (metadata.st_size < 0) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) file size is too large", fn_name);
      goto fail;
    }

    uintmax_t hinted_bytes = (uintmax_t)metadata.st_size;
    // The size hint must never override the caller's requested read limit.
    if (has_max_bytes && hinted_bytes > max_bytes) hinted_bytes = max_bytes;
    if (hinted_bytes >= SIZE_MAX) {
      if (!has_max_bytes) {
        snprintf(fail_msg, sizeof(fail_msg), "(%s) file size is too large", fn_name);
        goto fail;
      }
      // A bounded read still works without an unusably large size hint.
    } else {
      size_t wanted_capacity = (size_t)hinted_bytes + 1;
      if (wanted_capacity > capacity) capacity = wanted_capacity;
    }
  }

  data = malloc(capacity);
  if (data == NULL) goto allocation_failed;

  for (;;) {
    // An exact boundary does not imply truncation: probe once for EOF.
    // Borrowed stdin needs the extra byte pushed back so another read-file
    // call can consume the complete remaining stream.
    if (has_max_bytes && length == max_bytes) {
      int next_byte = fgetc(file);
      if (next_byte == EOF) {
        if (ferror(file)) goto read_failed;
        break;
      }

      if (borrowed_stream && ungetc(next_byte, file) == EOF) {
        snprintf(
          fail_msg,
          sizeof(fail_msg),
          "(%s) unable to restore the lookahead byte in “%s”",
          fn_name,
          filename
        );
        goto fail;
      }

      read_status = READ_FILE_STATUS_TRUNCATED;
      break;
    }

    // The last byte is reserved for the terminator, not file contents.
    // Check for EOF before growing a full buffer. A regular file whose size
    // matched the initial hint should not needlessly double its allocation.
    if (length == capacity - 1) {
      int next_byte = fgetc(file);
      if (next_byte == EOF) {
        if (ferror(file)) goto read_failed;
        break;
      }

      // Verify that doubling the capacity fits in size_t before multiplying.
      // Otherwise an overflow could cause realloc() to allocate too few bytes.
      if (capacity > SIZE_MAX / 2) {
        snprintf(fail_msg, sizeof(fail_msg), "(%s) file contents are too large", fn_name);
        goto fail;
      }

      size_t new_capacity = capacity * 2;
      // Never grow beyond the space needed for MAX_BYTES plus '\0'.
      // Check `max_bytes < SIZE_MAX` before adding the terminator byte,
      // since MAX_BYTES may be equal to SIZE_MAX on a 32-bit host.
      if (has_max_bytes && max_bytes < SIZE_MAX && new_capacity > max_bytes + 1)
        new_capacity = max_bytes + 1;
      char *grown = realloc(data, new_capacity);
      if (grown == NULL) goto allocation_failed;

      data = grown;
      capacity = new_capacity;

      // Preserve the lookahead byte that established the need to grow.
      data[length] = (char)(unsigned char)next_byte;
      length++;
    }

    size_t available = capacity - length - 1;
    if (has_max_bytes && available > max_bytes - length)
      available = max_bytes - length;
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
  data_adopted = true;

  // Report only bounded reads; unlimited reads leave the status unchanged.
  // The buffer is already tracked if lazy status registration should fail.
  if (has_max_bytes && !report_read_file_status(interpreter, read_status, &fail_msg)) goto fail;

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
    if (data != NULL && !data_adopted) {
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
