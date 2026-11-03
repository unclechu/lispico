#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "stdlib-arrays.h"
#include "stdlib-variables.h"
#include "stdlib-validation-utils.h"
#include "symbol-error.h"
#include "symbol-print.h"
#include "storage.h"
#include "wipe-memory.h"

// Find a named array variable. The caller reports any error.
static Symbol* get_array(
  Interpreter *interpreter,
  const Datum *name,
  const char *fn_name,
  char (*fail_msg)[ERR_MSG_SIZE]
) {
  char symbol_name[SYMBOL_NAME_SIZE] = {'\0'};

  if (!get_variable_name(name, &symbol_name)) {
    snprintf(*fail_msg, sizeof(*fail_msg), "(%s) invalid array name", fn_name);
    return NULL;
  }

  Symbol *symbol = symbol_lookup(interpreter, symbol_name);
  if (symbol == NULL) {
    snprintf(*fail_msg, sizeof(*fail_msg), "(%s) array “%s” is not defined", fn_name, symbol_name);
    return NULL;
  }

  if (symbol->kind != SYM_ARRAY) {
    snprintf(*fail_msg, sizeof(*fail_msg), "(%s) symbol “%s” is not an array", fn_name, symbol_name);
    return NULL;
  }

  return symbol;
}

// (arr) — Create an anonymous array from one or more values of the same type.
Datum std_arr(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "arr";

  if (argc == 0) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) cannot infer the element type of an empty array",
      fn_name
    );
    goto fail;
  }

  if (args[0].kind != SYM_VARIABLE) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects scalar elements", fn_name);
    goto fail;
  }

  ValueType element_type = args[0].as.value.type;
  size_t element_size = 0;

  // Determine single array element size
  switch (element_type) {
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
        show_ValueType(element_type)
      );
      goto fail;
  }

  // Validate everything before allocating runtime memory
  for (size_t i = 0; i < argc; ++i) {
    if (args[i].kind != SYM_VARIABLE) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) expects scalar elements", fn_name);
      goto fail;
    }
    if (args[i].as.value.type != element_type) {
      snprintf(
        fail_msg,
        sizeof(fail_msg),
        "(%s) expects all elements to have the same type (%s vs. %s)",
        fn_name,
        show_ValueType(element_type),
        show_ValueType(args[i].as.value.type)
      );
      goto fail;
    }
    if (element_type == TYPE_STRING) {
      String string = args[i].as.value.as.string;
      if (string.length >= SIZE_MAX || (string.length != 0 && string.data == NULL)) {
        snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid string element", fn_name);
        goto fail;
      }
    }
  }

  // Verifying `(argc * element_size) <= SIZE_MAX` without overflowing
  if (element_size > (SIZE_MAX / argc)) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) array allocation size is too large", fn_name);
    goto fail;
  }

  void *data = allocate_runtime_data(interpreter, argc * element_size);
  if (data == NULL) goto allocation_failed;

  for (size_t i = 0; i < argc; ++i) {
    Value value = args[i].as.value;
    switch (element_type) {
      case TYPE_U32: ((uint32_t *)data)[i] = value.as.u32; break;
      case TYPE_I32: ((int32_t *)data)[i] = value.as.i32; break;
      case TYPE_F32: ((float *)data)[i] = value.as.f32; break;
      case TYPE_STRING: {
        // For strings making an individual copy
        String source = value.as.string;

        // +1 for the `\0` terminator
        char *string_data = allocate_runtime_data(interpreter, source.length + 1);
        if (string_data == NULL) goto allocation_failed;

        // Cloning the string
        if (source.length != 0) memcpy(string_data, source.data, source.length);
        string_data[source.length] = '\0';

        ((String *)data)[i] = (String) { .data = string_data, .length = source.length };
        break;
      }
      default:
        snprintf(fail_msg, sizeof(fail_msg), "(arr) unsupported array element type");
        goto fail;
    }
  }

  return_value = (Datum) {
    .kind = SYM_ARRAY,
    .as.array = {
      .element_type = element_type,
      .length = (uint32_t)argc, // argc is bounded by EVAL_ARG_CAPACITY.
      .data = data,
    },
  };

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

// (arr-new) — Define a new, fixed-size, zero-initialized array variable.
Datum std_arr_new(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "arr-new";
  char symbol_name[SYMBOL_NAME_SIZE] = {'\0'};

  if (!validate_argc(interpreter, fn_name, &fail_msg, 3, argc)) return return_value;

  if (!get_variable_name(&args[0], &symbol_name)) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid array name", fn_name);
    goto fail;
  }

  if (
    args[1].kind != SYM_VARIABLE || args[1].as.value.type != TYPE_U32 ||
    args[2].kind != SYM_VARIABLE || args[2].as.value.type != TYPE_U32
  ) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects a type and length of u32", fn_name);
    goto fail;
  }

  ValueType element_type = (ValueType)args[1].as.value.as.u32;
  uint32_t length = args[2].as.value.as.u32;
  size_t element_size = 0;

  switch (element_type) {
    case TYPE_U32: element_size = sizeof(uint32_t); break;
    case TYPE_I32: element_size = sizeof(int32_t); break;
    case TYPE_F32: element_size = sizeof(float); break;
    case TYPE_STRING: element_size = sizeof(String); break;
    default:
      snprintf(fail_msg, sizeof(fail_msg), "(%s) unsupported array element type", fn_name);
      goto fail;
  }

  // Check byte-size overflow before allocating or defining a symbol.
  if (length != 0 && element_size > SIZE_MAX / (size_t)length) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) array allocation size is too large", fn_name);
    goto fail;
  }

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

  size_t size = (size_t)length * element_size;
  // allocate_runtime_data handles size == 0 by allocating one byte.
  void *data = allocate_runtime_data(interpreter, size);
  if (data == NULL) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) runtime allocation failed (OOM or allocation capacity reached)",
      fn_name
    );
    goto fail;
  }

  // Initialize all elements. String entries start as empty strings.
  wipe_memory(data, size == 0 ? 1 : size);

  Symbol *symbol = symbol_define(interpreter, symbol_name, SYM_ARRAY);
  if (symbol == NULL) {
    extract_error_message_from_symbol_define_error(interpreter, &fail_msg, symbol_name);
    goto fail;
  }

  symbol->as.array = (Array) { .element_type = element_type, .length = length, .data = data };
  return_value = (Datum) { .kind = SYM_ARRAY, .as.array = symbol->as.array };
  return return_value;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (arr-get) — Read a typed array element at a zero-based index.
Datum std_arr_get(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "arr-get";

  if (!validate_argc(interpreter, fn_name, &fail_msg, 2, argc)) return return_value;

  Array array;
  if (args[0].kind == SYM_ARRAY) {
    // Read directly from an evaluated array value.
    array = args[0].as.array;
  } else {
    // Otherwise, resolve the array by its variable name.
    Symbol *symbol = get_array(interpreter, &args[0], fn_name, &fail_msg);
    if (symbol == NULL) goto fail;
    array = symbol->as.array;
  }

  if (args[1].kind != SYM_VARIABLE || args[1].as.value.type != TYPE_U32) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects a u32 index", fn_name);
    goto fail;
  }

  uint32_t index = args[1].as.value.as.u32;
  if (index >= array.length || array.data == NULL) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) array index is out of bounds", fn_name);
    goto fail;
  }

  Value value = { .type = array.element_type };
  switch (array.element_type) {
    case TYPE_U32: value.as.u32 = ((const uint32_t *)array.data)[index]; break;
    case TYPE_I32: value.as.i32 = ((const int32_t *)array.data)[index]; break;
    case TYPE_F32: value.as.f32 = ((const float *)array.data)[index]; break;
    case TYPE_STRING: {
      String string = ((const String *)array.data)[index];

      if (string.length != 0 && string.data == NULL) {
        snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid string element", fn_name);
        goto fail;
      }

      // Zero-initialized string elements (in an array) of strings represent empty strings.
      //
      // This is a special form for arrays. Because you can create a new array
      // of strings with specified size but not give those strings any values
      // yet. Instead of causing a failure just default to an empty string
      // (while not actually allocating any real `String`s).
      //
      // This way it is more efficient when you reserve a an array large enough
      // but not necessarily use all of its elements. Though the evaluation
      // result reader (host) must account for this when reading arrays from the
      // `EvalOutcome` (do proper NULL-checks for array strings with
      // `String::length == 0`).
      if (string.data == NULL) string.data = "";

      value.as.string = string;
      break;
    }
    default:
      snprintf(fail_msg, sizeof(fail_msg), "(%s) unsupported array element type", fn_name);
      goto fail;
  }

  return_value = (Datum) { .kind = SYM_VARIABLE, .as.value = value };
  return return_value;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (arr-set) — Overwrite a typed array element without changing array length.
Datum std_arr_set(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "arr-set";

  if (!validate_argc(interpreter, fn_name, &fail_msg, 3, argc)) return return_value;

  Symbol *symbol = get_array(interpreter, &args[0], fn_name, &fail_msg);
  if (symbol == NULL) goto fail;

  if (args[1].kind != SYM_VARIABLE || args[1].as.value.type != TYPE_U32) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects a u32 index", fn_name);
    goto fail;
  }

  Array array = symbol->as.array;
  uint32_t index = args[1].as.value.as.u32;
  if (index >= array.length || array.data == NULL) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) array index is out of bounds", fn_name);
    goto fail;
  }

  if (args[2].kind != SYM_VARIABLE || args[2].as.value.type != array.element_type) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) array element type mismatch", fn_name);
    goto fail;
  }

  Value value = args[2].as.value;
  switch (array.element_type) {
    case TYPE_U32: ((uint32_t *)array.data)[index] = value.as.u32; break;
    case TYPE_I32: ((int32_t *)array.data)[index] = value.as.i32; break;
    case TYPE_F32: ((float *)array.data)[index] = value.as.f32; break;
    case TYPE_STRING: {
      String source = value.as.string;
      if ((source.length != 0 && source.data == NULL) || source.length == SIZE_MAX) {
        snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid string value", fn_name);
        goto fail;
      }

      // Copy strings into independently tracked, writable runtime memory.
      // Existing strings are kept until `clean_evaluation`, so no returned
      // string references become dangling during this evaluation.
      char *data = allocate_runtime_data(interpreter, source.length + 1);
      if (data == NULL) {
        snprintf(
          fail_msg,
          sizeof(fail_msg),
          "(%s) runtime allocation failed (OOM or allocation capacity reached)",
          fn_name
        );
        goto fail;
      }

      if (source.length != 0) memcpy(data, source.data, source.length);
      data[source.length] = '\0';
      ((String *)array.data)[index] = (String) {
        .data = data,
        .length = source.length,
      };
      // Return the stored copy instead of the original string.
      value.as.string = ((String *)array.data)[index];
      break;
    }
    default:
      snprintf(fail_msg, sizeof(fail_msg), "(%s) unsupported array element type", fn_name);
      goto fail;
  }

  // Return the value saved into the array element.
  return_value = (Datum) { .kind = SYM_VARIABLE, .as.value = value };
  return return_value;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (arr-size) — Return the element count of a named array as u32.
Datum std_arr_size(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "arr-size";

  if (!validate_argc(interpreter, fn_name, &fail_msg, 1, argc)) return return_value;

  Array array;
  if (args[0].kind == SYM_ARRAY) {
    // Read directly from an evaluated array value.
    array = args[0].as.array;
  } else {
    // Otherwise, resolve the array by its variable name.
    Symbol *symbol = get_array(interpreter, &args[0], fn_name, &fail_msg);
    if (symbol == NULL) goto fail;
    array = symbol->as.array;
  }

  return_value = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = { .type = TYPE_U32, .as.u32 = array.length },
  };

  return return_value;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (arr-type) — Return the type of the elements of a named array as u32.
Datum std_arr_type(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "arr-type";

  if (!validate_argc(interpreter, fn_name, &fail_msg, 1, argc)) return return_value;

  Array array;
  if (args[0].kind == SYM_ARRAY) {
    // Read directly from an evaluated array value.
    array = args[0].as.array;
  } else {
    // Otherwise, resolve the array by its variable name.
    Symbol *symbol = get_array(interpreter, &args[0], fn_name, &fail_msg);
    if (symbol == NULL) goto fail;
    array = symbol->as.array;
  }

  return_value = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = { .type = TYPE_U32, .as.u32 = (uint32_t)array.element_type },
  };

  return return_value;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (arr-slice), (arr-take), (arr-drop) — Copy a range into an independent array value
typedef enum {
  ARR_RANGE_SLICE,
  ARR_RANGE_TAKE,
  ARR_RANGE_DROP,
} ArrayRangeOperation;

static Datum std_arr_range(
  Interpreter *interpreter,
  const Datum *args,
  size_t argc,
  const char *fn_name,
  ArrayRangeOperation operation
) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};

  size_t expected_argc = operation == ARR_RANGE_SLICE ? 3 : 2;
  if (!validate_argc(interpreter, fn_name, &fail_msg, expected_argc, argc))
    return return_value;

  if (args[0].kind != SYM_ARRAY) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) expects an array as the first argument, got kind: %s",
      fn_name,
      show_SymbolKind(args[0].kind)
    );
    goto fail;
  }

  Array source = args[0].as.array;
  size_t element_size = 0;

  // Determine the size of one element to calculate allocation and copy offsets.
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

  if (args[1].kind != SYM_VARIABLE || args[1].as.value.type != TYPE_U32) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) expects a u32 %s",
      fn_name,
      (operation == ARR_RANGE_SLICE ? "starting index" : "element count")
    );
    goto fail;
  }

  uint32_t from = 0;
  uint32_t length = 0;
  uint32_t n = args[1].as.value.as.u32;

  switch (operation) {
    case ARR_RANGE_SLICE:
      if (args[2].kind != SYM_VARIABLE || args[2].as.value.type != TYPE_U32) {
        snprintf(fail_msg, sizeof(fail_msg), "(%s) expects a u32 length", fn_name);
        goto fail;
      }
      from = n;
      length = args[2].as.value.as.u32;
      break;
    case ARR_RANGE_TAKE:
      length = n;
      break;
    case ARR_RANGE_DROP:
      from = n;
      // Check before subtracting to prevent unsigned underflow.
      if (from > source.length) goto out_of_bounds;
      length = source.length - from;
      break;
  }

  // Validate the entire range without overflowing `from + length`.
  // An empty result is allowed, including when FROM == source.length.
  if (from > source.length || length > source.length - from) goto out_of_bounds;

  // Verify `(source.length * element_size) <= SIZE_MAX` without overflowing.
  // This also guarantees the copy offset and resulting allocation size fit.
  if (source.length != 0 && element_size > (SIZE_MAX / (size_t)source.length)) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) source array allocation size is too large",
      fn_name
    );
    goto fail;
  }

  size_t size = (size_t)length * element_size;

  // allocate_runtime_data handles size == 0 by allocating one byte.
  void *data = allocate_runtime_data(interpreter, size);
  if (data == NULL) goto allocation_failed;

  // Initialize the single byte allocated for a zero-length result.
  if (size == 0) wipe_memory(data, 1);

  if (source.element_type == TYPE_STRING) {
    // Copy and validate the selected strings in one traversal.
    for (uint32_t i = 0; i < length; ++i) {
      String string = ((const String *)source.data)[from + i];

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
        // Preserve an empty entry without allocating an individual string.
        ((String *)data)[i] = (String) { .data = NULL, .length = 0 };
        continue;
      }

      // +1 for the `\0` terminator, preserving embedded NUL bytes.
      char *string_data = allocate_runtime_data(interpreter, string.length + 1);
      if (string_data == NULL) goto allocation_failed;

      if (string.length != 0) memcpy(string_data, string.data, string.length);
      string_data[string.length] = '\0';
      ((String *)data)[i] = (String) { .data = string_data, .length = string.length };
    }
  } else if (size != 0) {
    // Numeric elements are raw scalar values and can be copied at once.
    const char *source_data = (const char *)source.data + ((size_t)from * element_size);
    memcpy(data, source_data, size);
  }

  return_value = (Datum) {
    .kind = SYM_ARRAY,
    .as.array = { .element_type = source.element_type, .length = length, .data = data },
  };

  return return_value;

  allocation_failed:
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) runtime allocation failed (OOM or allocation capacity reached)",
      fn_name
    );
    goto fail;

  out_of_bounds:
    snprintf(fail_msg, sizeof(fail_msg), "(%s) requested range is out of bounds", fn_name);
    goto fail;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (arr-slice) — Copy LENGTH elements starting at FROM
Datum std_arr_slice(Interpreter *interpreter, const Datum *args, size_t argc) {
  const char fn_name[] = "arr-slice";
  return std_arr_range(interpreter, args, argc, fn_name, ARR_RANGE_SLICE);
}

// (arr-take) — Copy the first N elements
Datum std_arr_take(Interpreter *interpreter, const Datum *args, size_t argc) {
  const char fn_name[] = "arr-take";
  return std_arr_range(interpreter, args, argc, fn_name, ARR_RANGE_TAKE);
}

// (arr-drop) — Skip the first N elements and copy the remainder
Datum std_arr_drop(Interpreter *interpreter, const Datum *args, size_t argc) {
  const char fn_name[] = "arr-drop";
  return std_arr_range(interpreter, args, argc, fn_name, ARR_RANGE_DROP);
}
