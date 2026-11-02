#include <inttypes.h>
#include <float.h>
#include <string.h>

#include "stdlib-types.h"
#include "stdlib-variables.h"
#include "stdlib-validation-utils.h"
#include "symbol-error.h"
#include "number.h"
#include "wipe-memory.h"
#include "evaluate.h"

// (type) — Explicit scalar type conversion
Datum std_type(const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "type";

  if (!validate_argc(fn_name, &fail_msg, 2, argc)) return return_value;
  if (
    args[0].kind != SYM_VARIABLE ||
    args[0].as.value.type != TYPE_U32 ||
    args[1].kind != SYM_VARIABLE
  ) {
    snprintf(
      fail_msg, sizeof(fail_msg),
      "(%s) expects a u32 type identifier and a scalar value",
      fn_name
    );
    goto fail;
  }

  uint32_t type_id = args[0].as.value.as.u32;
  Value source = args[1].as.value;
  ValueType target_type;

  switch (type_id) {
    case TYPE_U32: target_type = TYPE_U32; break;
    case TYPE_I32: target_type = TYPE_I32; break;
    case TYPE_F32: target_type = TYPE_F32; break;
    case TYPE_STRING: target_type = TYPE_STRING; break;
    default:
      snprintf(
        fail_msg,
        sizeof(fail_msg),
        "(%s) unknown target type identifier: %" PRIu32,
        fn_name,
        type_id
      );
      goto fail;
  }

  Value converted = { .type = target_type };

  // String to number conversion
  if (source.type == TYPE_STRING && target_type != TYPE_STRING) {
    NumberParseResult status = parse_number_text(source.as.string, target_type, &converted);

    switch (status) {
      case NUMBER_PARSE_OK: goto success;

      case NUMBER_PARSE_INVALID:
        snprintf(
          fail_msg,
          sizeof(fail_msg),
          "(%s) invalid numeric string for %s",
          fn_name,
          show_ValueType(target_type)
        );
        goto fail;

      case NUMBER_PARSE_INVALID_EXPONENT:
        snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid numeric exponent", fn_name);
        goto fail;

      case NUMBER_PARSE_OUT_OF_RANGE:
        goto out_of_range;

      case NUMBER_PARSE_TOO_LONG:
        snprintf(fail_msg, sizeof(fail_msg), "(%s) numeric string is too long", fn_name);
        goto fail;

      default:
        snprintf(fail_msg, sizeof(fail_msg), "(%s) unknown numeric parsing error", fn_name);
        goto fail;
    }
  }

  // Number to string, or string identity conversion
  if (target_type == TYPE_STRING) {
    // Nothing to do, it’s already a string
    if (source.type == TYPE_STRING) {
      converted = source;
      goto success;
    }

    char text[NUMBER_TEXT_CAPACITY];
    int length;

    switch (source.type) {
      case TYPE_U32: length = snprintf(text, sizeof(text), "%" PRIu32, source.as.u32); break;
      case TYPE_I32: length = snprintf(text, sizeof(text), "%" PRId32, source.as.i32); break;
      case TYPE_F32:
        length = snprintf(text, sizeof(text), "%.*g", FLT_DECIMAL_DIG, source.as.f32);
        break;
      default: goto unsupported;
    }

    if (length < 0 || (size_t)length >= sizeof(text)) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) unable to format number", fn_name);
      goto fail;
    }

    // +1 for the `\0` terminator
    char *data = allocate_runtime_data((size_t)length + 1);
    if (data == NULL) {
      snprintf(
        fail_msg,
        sizeof(fail_msg),
        "(%s) runtime allocation failed (OOM or allocation capacity reached)",
        fn_name
      );
      goto fail;
    }

    // +1 for the `\0` terminator
    memcpy(data, text, (size_t)length + 1);

    // Converted a number to string successfully
    converted.as.string = (String) { .data = data, .length = (size_t)length };
    goto success;
  }

  // Numeric-to-numeric conversions
  switch (source.type) {
    case TYPE_U32:
      switch (target_type) {
        case TYPE_U32: converted.as.u32 = source.as.u32; break;
        case TYPE_I32:
          if (source.as.u32 > INT32_MAX) goto out_of_range;
          converted.as.i32 = (int32_t)source.as.u32;
          break;
        case TYPE_F32: converted.as.f32 = (float)source.as.u32; break;
        default: goto unsupported;
      }
      break;

    case TYPE_I32:
      switch (target_type) {
        case TYPE_U32:
          if (source.as.i32 < 0) goto out_of_range;
          converted.as.u32 = (uint32_t)source.as.i32;
          break;
        case TYPE_I32: converted.as.i32 = source.as.i32; break;
        case TYPE_F32: converted.as.f32 = (float)source.as.i32; break;
        default: goto unsupported;
      }
      break;

    case TYPE_F32:
      switch (target_type) {
        case TYPE_U32:
          if (!(source.as.f32 >= 0.0f && source.as.f32 < 4294967296.0f))
            goto out_of_range;
          converted.as.u32 = (uint32_t)source.as.f32;
          break;
        case TYPE_I32:
          if (!(source.as.f32 >= -2147483648.0f && source.as.f32 < 2147483648.0f))
            goto out_of_range;
          converted.as.i32 = (int32_t)source.as.f32;
          break;
        case TYPE_F32: converted.as.f32 = source.as.f32; break;
        default: goto unsupported;
      }
      break;

    default: goto unsupported;
  }

  success:
    return_value = (Datum) {
      .kind = SYM_VARIABLE,
      .as.value = converted,
    };
    return return_value;

  out_of_range:
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) value is out of range for %s",
      fn_name,
      show_ValueType(target_type)
    );
    goto fail;

  unsupported:
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) unsupported source type: %s",
      fn_name,
      show_ValueType(source.type)
    );
    goto fail;

  fail:
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (type-of) — Get the `ValueType` of a primitive value or string
Datum std_type_of(const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "type-of";

  if (!validate_argc(fn_name, &fail_msg, 1, argc)) return return_value;

  ValueType type;
  switch (args[0].kind) {
    case SYM_EMPTY: type = TYPE_VOID; break;
    case SYM_VARIABLE: type = args[0].as.value.type; break;
    default:
      snprintf(
        fail_msg,
        sizeof(fail_msg),
        "(%s) expects a primitive value or string, got instead: %s",
        fn_name,
        show_SymbolKind(args[0].kind)
      );
      goto fail;
  }

  return_value = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = { .type = TYPE_U32, .as.u32 = (uint32_t)type },
  };

  return return_value;

  fail:
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (kind-of) — Get the `SymbolKind` of a registered name
Datum std_kind_of(const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "kind-of";
  char symbol_name[SYMBOL_NAME_SIZE] = {'\0'};

  if (!validate_argc(fn_name, &fail_msg, 1, argc)) return return_value;

  if (!get_variable_name(&args[0], &symbol_name)) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid variable name", fn_name);
    goto fail;
  }

  const Symbol *symbol = symbol_lookup(symbol_name);

  return_value = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = {
      .type = TYPE_U32,
      .as.u32 = (uint32_t)(symbol == NULL ? SYM_EMPTY : symbol->kind),
    },
  };

  return return_value;

  fail:
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}
