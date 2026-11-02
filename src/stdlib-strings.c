#include <string.h>

#include "stdlib-strings.h"
#include "stdlib-variables.h"
#include "stdlib-validation-utils.h"
#include "symbol-error.h"
#include "evaluate.h"
#include "wipe-memory.h"

// (str) — Concatenate zero or more strings
Datum std_str(const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "str";
  size_t length = 0;

  // Validate all arguments and calculate the resulting byte length.
  for (size_t i = 0; i < argc; ++i) {
    if (args[i].kind != SYM_VARIABLE || args[i].as.value.type != TYPE_STRING) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) all arguments must be strings", fn_name);
      goto fail;
    }

    String string = args[i].as.value.as.string;
    if (string.length != 0 && string.data == NULL) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid string argument", fn_name);
      goto fail;
    }

    // -1 to account for the '\0' terminator
    if (string.length > SIZE_MAX - length - 1) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) resulting string is too large", fn_name);
      goto fail;
    }

    length += string.length;
  }

  // +1 for the '\0' terminator
  char *data = allocate_runtime_data(length + 1);
  if (data == NULL) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) runtime allocation failed (OOM or allocation capacity reached)",
      fn_name
    );
    goto fail;
  }

  size_t offset = 0;
  for (size_t i = 0; i < argc; ++i) {
    String string = args[i].as.value.as.string;
    if (string.length != 0) {
      memcpy(data + offset, string.data, string.length);
      offset += string.length;
    }
  }

  // Convenience terminator; String.length remains authoritative.
  data[length] = '\0';

  return_value = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = {
      .type = TYPE_STRING,
      .as.string = { .data = data, .length = length },
    },
  };

  return return_value;

  fail:
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (str-size) — Return the byte length of a string as u32
Datum std_str_size(const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "str-size";

  if (!validate_argc(fn_name, &fail_msg, 1, argc)) return return_value;

  if (args[0].kind != SYM_VARIABLE || args[0].as.value.type != TYPE_STRING) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects a string", fn_name);
    goto fail;
  }

  String string = args[0].as.value.as.string;

  if (string.length != 0 && string.data == NULL) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid string argument", fn_name);
    goto fail;
  }

  // String lengths are stored as size_t but returned as u32
  if (string.length > UINT32_MAX) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) string length exceeds u32 range", fn_name);
    goto fail;
  }

  return_value = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = { .type = TYPE_U32, .as.u32 = (uint32_t)string.length },
  };

  return return_value;

  fail:
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (str-len) — Return the number of bytes before the first '\0' as u32
Datum std_str_len(const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "str-len";

  if (!validate_argc(fn_name, &fail_msg, 1, argc)) return return_value;

  if (args[0].kind != SYM_VARIABLE || args[0].as.value.type != TYPE_STRING) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects a string", fn_name);
    goto fail;
  }

  String string = args[0].as.value.as.string;

  if (string.length != 0 && string.data == NULL) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid string argument", fn_name);
    goto fail;
  }

  // If `\0` terminator is not found then for this string
  // (str-len) equals to (str-size).
  size_t length = string.length;

  // Find the first `\0` byte without reading beyond String.length.
  if (length != 0) {
    const char *terminator = memchr(string.data, '\0', length);
    if (terminator != NULL) length = (size_t)(terminator - string.data);
  }

  if (length > UINT32_MAX) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) string length exceeds u32 range", fn_name);
    goto fail;
  }

  return_value = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = { .type = TYPE_U32, .as.u32 = (uint32_t)length },
  };

  return return_value;

  fail:
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (str-char) — Return a string byte at the specified index as u32
Datum std_str_char(const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "str-char";

  if (argc != 1 && argc != 2) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects 1 or 2 arguments", fn_name);
    goto fail;
  }

  if (args[0].kind != SYM_VARIABLE || args[0].as.value.type != TYPE_STRING) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects a string as the first argument", fn_name);
    goto fail;
  }

  String string = args[0].as.value.as.string;
  uint32_t index = 0;

  if (argc == 1) {
    // The index can be omitted only for a single-byte string.
    if (string.length != 1) {
      snprintf(
        fail_msg,
        sizeof(fail_msg),
        "(%s) index is required unless string length is 1",
        fn_name
      );
      goto fail;
    }
  } else {
    if (args[1].kind != SYM_VARIABLE || args[1].as.value.type != TYPE_U32) {
      snprintf(
        fail_msg,
        sizeof(fail_msg),
        "(%s) expects a u32 index as the second argument",
        fn_name
      );
      goto fail;
    }

    index = args[1].as.value.as.u32;
  }

  // The index must point to a byte within String.length.
  if ((size_t)index >= string.length) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) string index is out of bounds", fn_name);
    goto fail;
  }

  if (string.data == NULL) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid string argument", fn_name);
    goto fail;
  }

  // Convert to unsigned char so bytes from 128 to 255 stay positive.
  return_value = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = { .type = TYPE_U32, .as.u32 = (unsigned char)string.data[index] },
  };

  return return_value;

  fail:
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (str-get) — Read a byte from a named string variable as u32
Datum std_str_get(const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "str-get";
  char symbol_name[SYMBOL_NAME_SIZE] = {'\0'};

  if (!validate_argc(fn_name, &fail_msg, 2, argc)) return return_value;

  if (!get_variable_name(&args[0], &symbol_name)) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid variable name", fn_name);
    goto fail;
  }

  if (args[1].kind != SYM_VARIABLE || args[1].as.value.type != TYPE_U32) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects a u32 index", fn_name);
    goto fail;
  }

  Symbol *symbol = symbol_lookup(symbol_name);

  if (symbol == NULL) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) variable “%s” is not defined", fn_name, symbol_name);
    goto fail;
  }

  if (symbol->kind != SYM_VARIABLE || symbol->as.variable.type != TYPE_STRING) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) symbol “%s” is not a string variable",
      fn_name,
      symbol_name
    );
    goto fail;
  }

  String string = symbol->as.variable.as.string;
  uint32_t index = args[1].as.value.as.u32;

  if ((size_t)index >= string.length) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) string index is out of bounds", fn_name);
    goto fail;
  }

  if (string.data == NULL) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid string variable", fn_name);
    goto fail;
  }

  // Convert to unsigned char so bytes from 128 to 255 stay positive.
  return_value = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = { .type = TYPE_U32, .as.u32 = (unsigned char)string.data[index] },
  };

  return return_value;

  fail:
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (str-set) — Modify bytes in a named string variable without allocation
Datum std_str_set(const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "str-set";
  char symbol_name[SYMBOL_NAME_SIZE] = {'\0'};

  if (argc != 2 && argc != 3) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects 2 or 3 arguments", fn_name);
    goto fail;
  }

  if (!get_variable_name(&args[0], &symbol_name)) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid variable name", fn_name);
    goto fail;
  }

  Symbol *symbol = symbol_lookup(symbol_name);

  if (symbol == NULL) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) variable “%s” is not defined", fn_name, symbol_name);
    goto fail;
  }

  if (symbol->kind != SYM_VARIABLE || symbol->as.variable.type != TYPE_STRING) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) symbol “%s” is not a string variable",
      fn_name,
      symbol_name
    );
    goto fail;
  }

  String destination = symbol->as.variable.as.string;

  if (destination.data == NULL) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid string variable", fn_name);
    goto fail;
  }

  char *data = (char *)destination.data;

  if (argc == 2) {
    // (str-set "NAME" STRING) — Replace from the beginning and zero-fill.
    if (args[1].kind != SYM_VARIABLE || args[1].as.value.type != TYPE_STRING) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) expects a string as the second argument", fn_name);
      goto fail;
    }

    String source = args[1].as.value.as.string;

    if (source.length != 0 && source.data == NULL) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid source string", fn_name);
      goto fail;
    }

    if (source.length > destination.length) goto out_of_bounds;

    // Copy first, then zero-fill the remaining bytes.
    // memmove() also handles overlapping source and destination.
    if (source.length != 0) memmove(data, source.data, source.length);
    memset(data + source.length, '\0', destination.length - source.length);
  } else {
    // (str-set "NAME" INDEX BYTE/STRING)
    if (args[1].kind != SYM_VARIABLE || args[1].as.value.type != TYPE_U32) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) expects a u32 index", fn_name);
      goto fail;
    }

    if (args[2].kind != SYM_VARIABLE) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) expects a u32 byte or string", fn_name);
      goto fail;
    }

    size_t index = args[1].as.value.as.u32;

    switch (args[2].as.value.type) {
      case TYPE_U32: {
        // (str-set "NAME" INDEX BYTE) — Replace exactly one byte.
        uint32_t byte = args[2].as.value.as.u32;

        if (byte > 255) {
          snprintf(fail_msg, sizeof(fail_msg), "(%s) byte must be between 0 and 255", fn_name);
          goto fail;
        }

        if (index >= destination.length) goto out_of_bounds;

        // data[index] = (char)(unsigned char)byte;
        // For better portability between platforms.
        // It makes storing values 128–255 explicit, even on platforms where
        // plain `char` is signed.
        ((unsigned char *)data)[index] = (unsigned char)byte;

        break;
      }

      case TYPE_STRING: {
        // (str-set "NAME" INDEX STRING) — Replace a range of bytes.
        String source = args[2].as.value.as.string;

        if (source.length != 0 && source.data == NULL) {
          snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid source string", fn_name);
          goto fail;
        }

        // Check the entire range before modifying the destination.
        if (index > destination.length || source.length > destination.length - index)
          goto out_of_bounds;

        // Preserve the original string length and all untouched bytes.
        // `memmove` handles overlapping source and destination.
        if (source.length != 0) memmove(data + index, source.data, source.length);
        break;
      }

      default:
        snprintf(fail_msg, sizeof(fail_msg), "(%s) expects a u32 byte or string", fn_name);
        goto fail;
    }
  }

  // Return the modified string variable, like (set).
  return_value = (Datum) { .kind = SYM_VARIABLE, .as.value = symbol->as.variable };
  return return_value;

  out_of_bounds:
    snprintf(fail_msg, sizeof(fail_msg), "(%s) string write is out of bounds", fn_name);
    goto fail;

  fail:
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (str-copy) — Create an independent copy of a string with optional length limit
Datum std_str_copy(const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "str-copy";

  if (argc != 1 && argc != 2) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects 1 or 2 arguments", fn_name);
    goto fail;
  }

  if (args[0].kind != SYM_VARIABLE || args[0].as.value.type != TYPE_STRING) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects a string as the first argument", fn_name);
    goto fail;
  }

  String source = args[0].as.value.as.string;

  if (source.length != 0 && source.data == NULL) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid source string", fn_name);
    goto fail;
  }

  // By default, copy the entire string, including embedded `\0` bytes.
  size_t length = source.length;

  if (argc == 2) {
    if (args[1].kind != SYM_VARIABLE || args[1].as.value.type != TYPE_U32) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) expects a u32 length", fn_name);
      goto fail;
    }

    length = args[1].as.value.as.u32;

    if (length > source.length) {
      snprintf(
        fail_msg,
        sizeof(fail_msg),
        "(%s) requested length exceeds source string length",
        fn_name
      );
      goto fail;
    }
  }

  // Reserve one additional byte for a convenience `\0` terminator.
  if (length == SIZE_MAX) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) resulting string is too large", fn_name);
    goto fail;
  }

  // +1 for the final string `\0` terminator
  char *data = allocate_runtime_data(length + 1);
  if (data == NULL) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) runtime allocation failed (OOM or allocation capacity reached)",
      fn_name
    );
    goto fail;
  }

  // Copy exactly `length` bytes, regardless of embedded `\0` bytes.
  if (length != 0) memcpy(data, source.data, length);
  data[length] = '\0';

  return_value = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = {
      .type = TYPE_STRING,
      .as.string = { .data = data, .length = length },
    },
  };

  return return_value;

  fail:
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}
