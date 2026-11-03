#include <math.h>

#include "stdlib-numeric-fns.h"
#include "stdlib-validation-utils.h"
#include "symbol-error.h"
#include "wipe-memory.h"
#include "evaluate.h"

// (abs) — Absolute value of a numeric value
Datum std_abs(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "abs";

  if (!validate_unary_numeric_input(interpreter, fn_name, &fail_msg, args, argc))
    return return_value;

  Value value = args[0].as.value;

  switch (value.type) {
    case TYPE_U32: break; // For u32 the operation always returns the same value

    case TYPE_I32:
      if (value.as.i32 == INT32_MIN) {
        snprintf(fail_msg, sizeof(fail_msg), "(%s) result exceeds i32 range", fn_name);
        goto fail;
      }
      if (value.as.i32 < 0) value.as.i32 = -value.as.i32;
      break;

    case TYPE_F32:
      if (!isfinite(value.as.f32)) goto invalid_value;
      value.as.f32 = fabsf(value.as.f32);
      break;

    default:
      report_unexpected_value_type(interpreter, fn_name, &fail_msg, value.type);
      return return_value;
  }

  return_value = (Datum) { .kind = SYM_VARIABLE, .as.value = value };
  return return_value;

  invalid_value:
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects a finite numeric value", fn_name);
    goto fail;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// Select the minimum or maximum of one or more numeric values.
static Datum std_minmax_generic(
  Interpreter *interpreter,
  const Datum *args,
  size_t argc,
  const char *fn_name,
  bool is_min
) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};

  if (argc == 0) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects at least 1 argument", fn_name);
    goto fail;
  }

  if (args[0].kind != SYM_VARIABLE) goto invalid_value;

  Value selected = args[0].as.value;

  switch (selected.type) {
    case TYPE_U32:
    case TYPE_I32:
      break;

    case TYPE_F32:
      if (!isfinite(selected.as.f32)) goto invalid_value;
      break;

    default: goto invalid_value;
  }

  // Compare all remaining arguments against the selected value.
  for (size_t i = 1; i < argc; i++) {
    if (args[i].kind != SYM_VARIABLE) goto invalid_value;

    Value current = args[i].as.value;

    if (current.type != selected.type) {
      snprintf(
        fail_msg,
        sizeof(fail_msg),
        "(%s) expects all arguments to be of the same type",
        fn_name
      );
      goto fail;
    }

    bool replace = false;

    switch (selected.type) {
      case TYPE_U32:
        replace = is_min ? current.as.u32 < selected.as.u32 : current.as.u32 > selected.as.u32;
        break;

      case TYPE_I32:
        replace = is_min ? current.as.i32 < selected.as.i32 : current.as.i32 > selected.as.i32;
        break;

      case TYPE_F32:
        if (!isfinite(current.as.f32)) goto invalid_value;
        replace = is_min ? current.as.f32 < selected.as.f32 : current.as.f32 > selected.as.f32;
        break;

      default: goto invalid_value;
    }

    if (replace) selected = current;
  }

  return_value = (Datum) { .kind = SYM_VARIABLE, .as.value = selected };
  return return_value;

  invalid_value:
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) expects finite numeric values (u32, i32, or f32)",
      fn_name
    );
    goto fail;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (min) — Return the smallest of one or more values
Datum std_min(Interpreter *interpreter, const Datum *args, size_t argc) {
  return std_minmax_generic(interpreter, args, argc, "min", true);
}

// (max) — Return the largest of one or more values
Datum std_max(Interpreter *interpreter, const Datum *args, size_t argc) {
  return std_minmax_generic(interpreter, args, argc, "max", false);
}

// (clamp VALUE MIN MAX) — Restrict a numeric value to an inclusive range
Datum std_clamp(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "clamp";

  if (!validate_argc(interpreter, fn_name, &fail_msg, 3, argc)) return return_value;

  if (
    args[0].kind != SYM_VARIABLE ||
    args[1].kind != SYM_VARIABLE ||
    args[2].kind != SYM_VARIABLE
  ) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects three numeric values", fn_name);
    goto fail;
  }

  ValueType type = args[0].as.value.type;

  if (type != args[1].as.value.type || type != args[2].as.value.type) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects all arguments of the same type", fn_name);
    goto fail;
  }

  Value value = { .type = type };

  switch (type) {
    case TYPE_U32: {
      uint32_t x = args[0].as.value.as.u32;
      uint32_t min = args[1].as.value.as.u32;
      uint32_t max = args[2].as.value.as.u32;

      if (min > max) goto invalid_range;
      value.as.u32 = x < min ? min : x > max ? max : x;
      break;
    }

    case TYPE_I32: {
      int32_t x = args[0].as.value.as.i32;
      int32_t min = args[1].as.value.as.i32;
      int32_t max = args[2].as.value.as.i32;

      if (min > max) goto invalid_range;
      value.as.i32 = x < min ? min : x > max ? max : x;
      break;
    }

    case TYPE_F32: {
      float x = args[0].as.value.as.f32;
      float min = args[1].as.value.as.f32;
      float max = args[2].as.value.as.f32;

      if (!isfinite(x) || !isfinite(min) || !isfinite(max)) goto invalid_value;
      if (min > max) goto invalid_range;

      value.as.f32 = x < min ? min : x > max ? max : x;
      break;
    }

    default:
      report_unexpected_value_type(interpreter, fn_name, &fail_msg, type);
      return return_value;
  }

  return_value = (Datum) { .kind = SYM_VARIABLE, .as.value = value };
  return return_value;

  invalid_value:
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects finite numeric values", fn_name);
    goto fail;

  invalid_range:
    snprintf(fail_msg, sizeof(fail_msg), "(%s) minimum exceeds maximum", fn_name);
    goto fail;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}
