#include <string.h>

#include "stdlib-variables.h"
#include "stdlib-validation-utils.h"
#include "symbol-error.h"
#include "symbol-print.h"
#include "wipe-memory.h"
#include "evaluate.h"

// Extract variable name from a `Datum`
bool get_variable_name(const Datum *datum, char (*name)[SYMBOL_NAME_SIZE]) {
  if (datum->kind != SYM_VARIABLE || datum->as.value.type != TYPE_STRING) return false;
  String string = datum->as.value.as.string;

  if (
    string.data == NULL ||
    string.length == 0 ||
    string.length >= sizeof(*name) ||
    // The string length must not include string terminator
    memchr(string.data, '\0', string.length) != NULL
  ) return false;

  memcpy(*name, string.data, string.length);
  (*name)[string.length] = '\0';
  return true;
}

// (freeze "NAME") — Make any existing symbol immutable for this evaluation.
Datum std_freeze(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "freeze";
  char symbol_name[SYMBOL_NAME_SIZE] = {'\0'};

  if (!validate_argc(interpreter, fn_name, &fail_msg, 1, argc)) return return_value;

  if (!get_variable_name(&args[0], &symbol_name)) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid symbol name", fn_name);
    goto fail;
  }

  Symbol *symbol = symbol_lookup(interpreter, symbol_name);
  if (symbol == NULL) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) symbol “%s” is not defined", fn_name, symbol_name);
    goto fail;
  }

  // Freezing an already frozen symbol is harmless.
  symbol->frozen = true;
  return return_value;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (var) — Define a new variable, inferring its type from the initial value
Datum std_var(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "var";
  char symbol_name[SYMBOL_NAME_SIZE] = {'\0'};

  if (!validate_argc(interpreter, fn_name, &fail_msg, 2, argc)) return return_value;

  if (!get_variable_name(&args[0], &symbol_name)) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid variable name", fn_name);
    goto fail;
  }

  if (args[1].kind == SYM_ARRAY) {
    // Array variables own independent storage, just like string variables.
    // Copy the array value before defining its name so allocation failures
    // cannot leave a partially initialized symbol in the table.
    Array source = args[1].as.array;
    size_t element_size = 0;

    // Determine the size of one array element
    switch (source.element_type) {
      case TYPE_U32: element_size = sizeof(uint32_t); break;
      case TYPE_I32: element_size = sizeof(int32_t); break;
      case TYPE_F32: element_size = sizeof(float); break;
      case TYPE_STRING: element_size = sizeof(String); break;
      default:
        snprintf(
          fail_msg,
          sizeof(fail_msg),
          "(%s) unsupported array element type: %s",
          fn_name,
          show_ValueType(source.element_type)
        );
        goto fail;
    }

    if (source.length != 0 && source.data == NULL) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid source array", fn_name);
      goto fail;
    }

    // Verify `(source.length * element_size) <= SIZE_MAX` without overflowing.
    // Zero-length array values are valid and retain their element type.
    if (source.length != 0 && element_size > (SIZE_MAX / (size_t)source.length)) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) array allocation size is too large", fn_name);
      goto fail;
    }

    // Avoid allocating the copy if the requested symbol name already exists.
    if (symbol_lookup(interpreter, symbol_name) != NULL) {
      snprintf(
        fail_msg,
        sizeof(fail_msg),
        "(%s) symbol “%s” is already defined",
        fn_name,
        symbol_name
      );
      goto fail;
    }

    size_t size = (size_t)source.length * element_size;

    // allocate_runtime_data handles size == 0 by allocating one byte.
    void *data = allocate_runtime_data(interpreter, size);
    if (data == NULL) goto array_allocation_failed;

    // Also initialize the one allocated byte for empty arrays.
    wipe_memory(data, size == 0 ? 1 : size);

    if (source.element_type == TYPE_STRING) {
      for (uint32_t i = 0; i < source.length; ++i) {
        String string = ((const String *)source.data)[i];

        // Validate the string before attempting to copy it.
        // NULL data is only valid for zero-length strings.
        if (string.length >= SIZE_MAX || (string.length != 0 && string.data == NULL)) {
          snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid string array element", fn_name);
          goto fail;
        }

        // Preserve the zero-initialized empty-string representation.
        // It has no string buffer to copy, so no allocation is necessary.
        if (string.data == NULL) continue;

        // +1 for the `\0` terminator, preserving embedded NUL bytes.
        char *string_data = allocate_runtime_data(interpreter, string.length + 1);
        if (string_data == NULL) goto array_allocation_failed;

        // Clone each string into independent runtime storage.
        if (string.length != 0) memcpy(string_data, string.data, string.length);
        string_data[string.length] = '\0';

        ((String *)data)[i] = (String) { .data = string_data, .length = string.length };
      }
    } else if (size != 0) {
      // Numeric arrays contain only raw scalar values.
      memcpy(data, source.data, size);
    }

    Symbol *symbol = symbol_define(interpreter, symbol_name, SYM_ARRAY);
    if (symbol == NULL) {
      extract_error_message_from_symbol_define_error(interpreter, &fail_msg, symbol_name);
      goto fail;
    }

    symbol->as.array = (Array) {
      .element_type = source.element_type,
      .length = source.length,
      .data = data,
    };

    return_value = (Datum) { .kind = SYM_ARRAY, .as.array = symbol->as.array };
    return return_value;
  }

  if (args[1].kind != SYM_VARIABLE) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) expects a scalar or array value, got kind: %s",
      fn_name,
      show_SymbolKind(args[1].kind)
    );
    goto fail;
  }

  switch (args[1].as.value.type) {
    case TYPE_U32:
    case TYPE_I32:
    case TYPE_F32:
    case TYPE_STRING:
      break;
    default:
      report_unexpected_value_type(interpreter, fn_name, &fail_msg, args[1].as.value.type);
      return return_value;
  }

  Value value = args[1].as.value;

  if (value.type == TYPE_STRING) {
    String string = value.as.string;

    if (string.length == SIZE_MAX || (string.length != 0 && string.data == NULL)) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid string value", fn_name);
      goto fail;
    }

    // String variables own independent writable storage.
    // Reserve one extra byte for a convenient '\0' terminator.
    char *data = allocate_runtime_data(interpreter, string.length + 1);
    if (data == NULL) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) runtime allocation failed", fn_name);
      goto fail;
    }

    if (string.length != 0) memcpy(data, string.data, string.length);
    data[string.length] = '\0';

    value.as.string.data = data;
  }

  // Important: Allocate the string before calling `symbol_define`, rather than
  // using that replacement verbatim after symbol creation. Otherwise an
  // allocation failure could leave a partially initialized symbol in the table.
  Symbol *symbol = symbol_define(interpreter, symbol_name, SYM_VARIABLE);

  if (symbol == NULL) {
    extract_error_message_from_symbol_define_error(interpreter, &fail_msg, symbol_name);
    goto fail;
  }

  symbol->as.variable = value;

  return_value = (Datum) { .kind = SYM_VARIABLE, .as.value = value };
  return return_value;

  array_allocation_failed:
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) runtime allocation failed (OOM or allocation capacity reached)",
      fn_name
    );
    goto fail;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (set) — Overwrite an existing variable
Datum std_set(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "set";
  char symbol_name[SYMBOL_NAME_SIZE] = {'\0'};

  if (!validate_argc(interpreter, fn_name, &fail_msg, 2, argc)) return return_value;

  if (!get_variable_name(&args[0], &symbol_name)) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid variable name", fn_name);
    goto fail;
  }

  if (args[1].kind == SYM_ARRAY) {
    Symbol *symbol = symbol_lookup(interpreter, symbol_name);

    if (symbol == NULL) {
      snprintf(
        fail_msg,
        sizeof(fail_msg),
        "(%s) variable “%s” is not defined",
        fn_name,
        symbol_name
      );
      goto fail;
    }

    if (!validate_symbol_mutable(symbol, fn_name, &fail_msg)) goto fail;

    if (symbol->kind != SYM_ARRAY) {
      snprintf(
        fail_msg,
        sizeof(fail_msg),
        "(%s) symbol “%s” is not an array, got kind: %s",
        fn_name,
        symbol_name,
        show_SymbolKind(symbol->kind)
      );
      goto fail;
    }

    Array destination = symbol->as.array;
    Array source = args[1].as.array;
    size_t element_size = 0;

    if (destination.element_type != source.element_type) {
      snprintf(
        fail_msg,
        sizeof(fail_msg),
        "(%s) array element type mismatch: expected %s, got %s",
        fn_name,
        show_ValueType(destination.element_type),
        show_ValueType(source.element_type)
      );
      goto fail;
    }

    // The destination keeps its original capacity and element type.
    if (source.length > destination.length) {
      snprintf(
        fail_msg,
        sizeof(fail_msg),
        "(%s) source array has %zu elements, destination has %zu",
        fn_name,
        (size_t)source.length,
        (size_t)destination.length
      );
      goto fail;
    }

    if (
      (destination.length != 0 && destination.data == NULL) ||
      (source.length != 0 && source.data == NULL)
    ) {
      snprintf(
        fail_msg,
        sizeof(fail_msg),
        "(%s) invalid source or destination array",
        fn_name
      );
      goto fail;
    }

    // Determine the size of an array element for copying and zero-filling.
    switch (destination.element_type) {
      case TYPE_U32: element_size = sizeof(uint32_t); break;
      case TYPE_I32: element_size = sizeof(int32_t); break;
      case TYPE_F32: element_size = sizeof(float); break;
      case TYPE_STRING: element_size = sizeof(String); break;
      default:
        snprintf(
          fail_msg,
          sizeof(fail_msg),
          "(%s) unsupported array element type: %s",
          fn_name,
          show_ValueType(destination.element_type)
        );
        goto fail;
    }

    // Verify `(destination.length * element_size) <= SIZE_MAX` without
    // overflowing. Since `source.length <= destination.length`, this also
    // guarantees the copied prefix and cleared remainder sizes fit.
    if (destination.length != 0 && element_size > (SIZE_MAX / (size_t)destination.length)) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) array size is too large", fn_name);
      goto fail;
    }

    // Assigning an array to itself does not require copying or allocating.
    if (source.data == destination.data && source.length == destination.length) {
      return_value = (Datum) { .kind = SYM_ARRAY, .as.array = symbol->as.array };
      return return_value;
    }

    if (destination.element_type == TYPE_STRING) {
      // Validate and copy each source string in one traversal. The old string
      // buffers remain tracked until `clean_evaluation()`, just like `(arr-set)`.
      for (uint32_t i = 0; i < source.length; ++i) {
        String string = ((const String *)source.data)[i];

        // NULL data is valid only for a zero-initialized empty string.
        if (string.length >= SIZE_MAX || (string.length != 0 && string.data == NULL)) {
          snprintf(
            fail_msg,
            sizeof(fail_msg),
            "(%s) invalid string array element",
            fn_name
          );
          goto fail;
        }

        if (string.data == NULL) {
          ((String *)destination.data)[i] = (String) { .data = NULL, .length = 0 };
          continue;
        }

        // +1 for the final `\0` terminator, preserving embedded NUL bytes.
        char *string_data = allocate_runtime_data(interpreter, string.length + 1);
        if (string_data == NULL) goto array_allocation_failed;

        if (string.length != 0) memcpy(string_data, string.data, string.length);
        string_data[string.length] = '\0';

        ((String *)destination.data)[i] = (String) {
          .data = string_data,
          .length = string.length,
        };
      }

      // Clear all unused string elements without allocating empty strings.
      for (uint32_t i = source.length; i < destination.length; ++i)
        ((String *)destination.data)[i] = (String) { .data = NULL, .length = 0 };
    } else {
      size_t copied_size = (size_t)source.length * element_size;
      size_t remaining_size = (size_t)(destination.length - source.length) * element_size;

      // memmove handles assigning an array from its own storage.
      if (copied_size != 0) memmove(destination.data, source.data, copied_size);

      // Numeric array tails are zero-initialized, as with arr-new.
      if (remaining_size != 0) wipe_memory((char *)destination.data + copied_size, remaining_size);
    }

    // The destination's element type, length, and storage pointer are unchanged.
    return_value = (Datum) { .kind = SYM_ARRAY, .as.array = symbol->as.array };
    return return_value;
  }

  if (args[1].kind != SYM_VARIABLE) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) expects a scalar or array value, got kind: %s",
      fn_name,
      show_SymbolKind(args[1].kind)
    );
    goto fail;
  }

  Symbol *symbol = symbol_lookup(interpreter, symbol_name);

  if (symbol == NULL) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) variable “%s” is not defined",
      fn_name,
      symbol_name
    );
    goto fail;
  }

  if (!validate_symbol_mutable(symbol, fn_name, &fail_msg)) goto fail;

  if (symbol->kind != SYM_VARIABLE) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) symbol “%s” is not a variable",
      fn_name,
      symbol_name
    );
    goto fail;
  }

  if (symbol->as.variable.type != args[1].as.value.type) {
    snprintf(
      fail_msg, sizeof(fail_msg),
      "(%s) variable type mismatch: expected %s, got %s",
      fn_name,
      show_ValueType(symbol->as.variable.type),
      show_ValueType(args[1].as.value.type)
    );
    goto fail;
  }

  switch (args[1].as.value.type) {
    case TYPE_U32:
    case TYPE_I32:
    case TYPE_F32:
    case TYPE_STRING:
      break;
    default:
      report_unexpected_value_type(interpreter, fn_name, &fail_msg, args[1].as.value.type);
      return return_value;
  }

  if (args[1].as.value.type == TYPE_STRING) {
    String destination = symbol->as.variable.as.string;
    String source = args[1].as.value.as.string;

    if (
      destination.data == NULL ||
      (source.length != 0 && source.data == NULL) ||
      source.length > destination.length
    ) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid string or out-of-bounds write", fn_name);
      goto fail;
    }

    // Overwrite without changing the string length or allocating memory.
    // `memmove` is safe even if source and destination overlap.
    char *data = (char *)destination.data;
    if (source.length != 0) memmove(data, source.data, source.length);

    // Clear (`\0`-fill) any unused bytes after the new content.
    memset(data + source.length, '\0', destination.length - source.length);
  } else {
    symbol->as.variable = args[1].as.value;
  }

  return_value = (Datum) { .kind = SYM_VARIABLE, .as.value = symbol->as.variable };
  return return_value;

  array_allocation_failed:
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) runtime allocation failed (OOM or allocation capacity reached)",
      fn_name
    );
    goto fail;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (override) — Completely replace any existing symbol with a new scalar or array variable
Datum std_override(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "override";
  char symbol_name[SYMBOL_NAME_SIZE] = {'\0'};

  if (!validate_argc(interpreter, fn_name, &fail_msg, 2, argc)) return return_value;

  if (!get_variable_name(&args[0], &symbol_name)) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid variable name", fn_name);
    goto fail;
  }

  Symbol *symbol = symbol_lookup(interpreter, symbol_name);
  if (symbol == NULL) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) symbol “%s” is not defined",
      fn_name,
      symbol_name
    );
    goto fail;
  }

  if (!validate_symbol_mutable(symbol, fn_name, &fail_msg)) goto fail;

  if (args[1].kind == SYM_ARRAY) {
    Array source = args[1].as.array;
    size_t element_size = 0;

    // Determine the size of one array element, exactly as with `(var)`.
    switch (source.element_type) {
      case TYPE_U32: element_size = sizeof(uint32_t); break;
      case TYPE_I32: element_size = sizeof(int32_t); break;
      case TYPE_F32: element_size = sizeof(float); break;
      case TYPE_STRING: element_size = sizeof(String); break;
      default:
        snprintf(
          fail_msg,
          sizeof(fail_msg),
          "(%s) unsupported array element type: %s",
          fn_name,
          show_ValueType(source.element_type)
        );
        goto fail;
    }

    if (source.length != 0 && source.data == NULL) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid source array", fn_name);
      goto fail;
    }

    // Verify `(source.length * element_size) <= SIZE_MAX` without overflowing.
    // Zero-length array values are valid and retain their element type.
    if (source.length != 0 && element_size > (SIZE_MAX / (size_t)source.length)) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) array allocation size is too large", fn_name);
      goto fail;
    }

    size_t size = (size_t)source.length * element_size;

    // Allocate and copy the replacement before touching the existing symbol.
    // Source and destination may refer to the same array, so allocating first
    // also makes `(override "NAME" NAME)` safe.
    void *data = allocate_runtime_data(interpreter, size);
    if (data == NULL) goto allocation_failed;

    // `allocate_runtime_data` allocates one byte for an empty array.
    wipe_memory(data, size == 0 ? 1 : size);

    if (source.element_type == TYPE_STRING) {
      for (uint32_t i = 0; i < source.length; ++i) {
        String string = ((const String *)source.data)[i];

        // NULL data is valid only for a zero-initialized empty string.
        if (string.length >= SIZE_MAX || (string.length != 0 && string.data == NULL)) {
          snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid string array element", fn_name);
          goto fail;
        }

        if (string.data == NULL) continue;

        // Preserve embedded NUL bytes, and reserve +1 for the terminator.
        char *string_data = allocate_runtime_data(interpreter, string.length + 1);
        if (string_data == NULL) goto allocation_failed;

        if (string.length != 0) memcpy(string_data, string.data, string.length);
        string_data[string.length] = '\0';

        ((String *)data)[i] = (String) { .data = string_data, .length = string.length };
      }
    } else if (size != 0) {
      // Numeric arrays contain raw scalar values.
      memcpy(data, source.data, size);
    }

    // Commit the complete replacement. Old buffers remain runtime-tracked
    // until evaluation cleanup, so any evaluated aliases stay valid.
    symbol->kind = SYM_ARRAY;
    symbol->as.array = (Array) {
      .element_type = source.element_type,
      .length = source.length,
      .data = data,
    };

    return_value = (Datum) { .kind = SYM_ARRAY, .as.array = symbol->as.array };
    return return_value;
  }

  if (args[1].kind != SYM_VARIABLE) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) expects a scalar or array value, got kind: %s",
      fn_name,
      show_SymbolKind(args[1].kind)
    );
    goto fail;
  }

  switch (args[1].as.value.type) {
    case TYPE_U32:
    case TYPE_I32:
    case TYPE_F32:
    case TYPE_STRING:
      break;
    default:
      snprintf(
        fail_msg,
        sizeof(fail_msg),
        "(%s) unsupported value type: %s",
        fn_name,
        show_ValueType(args[1].as.value.type)
      );
      goto fail;
  }

  Value value = args[1].as.value;

  if (value.type == TYPE_STRING) {
    String string = value.as.string;

    if (string.length >= SIZE_MAX || (string.length != 0 && string.data == NULL)) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid string value", fn_name);
      goto fail;
    }

    // Strings own independent writable storage and are NUL-terminated.
    // Copy before replacing the symbol so self-assignment stays safe.
    char *data = allocate_runtime_data(interpreter, string.length + 1);
    if (data == NULL) goto allocation_failed;

    if (string.length != 0) memcpy(data, string.data, string.length);
    data[string.length] = '\0';
    value.as.string.data = data;
  }

  // The existing symbol may be a variable, array, label, or native function.
  // Its previous kind and storage do not restrict the new value.
  symbol->as.variable = value;
  symbol->kind = SYM_VARIABLE;

  return_value = (Datum) { .kind = SYM_VARIABLE, .as.value = value };
  return return_value;

  allocation_failed:
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) runtime allocation failed (OOM or allocation capacity reached)",
      fn_name
    );
    goto fail;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}
