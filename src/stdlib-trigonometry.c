#include <math.h>

#include "stdlib-trigonometry.h"
#include "stdlib-validation-utils.h"
#include "symbol-error.h"
#include "wipe-memory.h"
#include "evaluate.h"

// Apply a unary f32 trigonometric function.
static Datum std_unary_trigonometry(
  Interpreter *interpreter,
  const Datum *args,
  size_t argc,
  const char *fn_name,
  float (*operation)(float),
  bool unit_interval_only
) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};

  if (!validate_unary_numeric_input(interpreter, fn_name, &fail_msg, args, argc))
    return return_value;

  if (args[0].as.value.type != TYPE_F32) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects an f32 value", fn_name);
    goto fail;
  }

  float input = args[0].as.value.as.f32;

  if (!isfinite(input)) goto invalid_value;

  // (asin) and (acos) are defined for real inputs only within [-1, 1].
  if (unit_interval_only && (input < -1.0f || input > 1.0f))
    goto invalid_value;

  float result = operation(input);

  if (!isfinite(result)) goto invalid_value;

  return_value = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = { .type = TYPE_F32, .as.f32 = result },
  };

  return return_value;

  invalid_value:
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) input is outside the finite mathematical domain",
      fn_name
    );
    goto fail;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// Apply a binary f32 trigonometric or geometric function.
static Datum std_binary_trigonometry(
  Interpreter *interpreter,
  const Datum *args,
  size_t argc,
  const char *fn_name,
  float (*operation)(float, float)
) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};

  if (!validate_two_same_type_variables_inputs(interpreter, fn_name, &fail_msg, args, argc))
    return return_value;

  if (args[0].as.value.type != TYPE_F32) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects f32 arguments", fn_name);
    goto fail;
  }

  float a = args[0].as.value.as.f32;
  float b = args[1].as.value.as.f32;

  if (!isfinite(a) || !isfinite(b)) goto invalid_value;

  float result = operation(a, b);

  if (!isfinite(result)) goto invalid_value;

  return_value = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = { .type = TYPE_F32, .as.f32 = result },
  };

  return return_value;

  invalid_value:
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) invalid floating-point input or result",
      fn_name
    );
    goto fail;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (sin) — Sine of an angle in radians
Datum std_sin(Interpreter *interpreter, const Datum *args, size_t argc) {
  return std_unary_trigonometry(interpreter, args, argc, "sin", sinf, false);
}

// (cos) — Cosine of an angle in radians
Datum std_cos(Interpreter *interpreter, const Datum *args, size_t argc) {
  return std_unary_trigonometry(interpreter, args, argc, "cos", cosf, false);
}

// (tan) — Tangent of an angle in radians
Datum std_tan(Interpreter *interpreter, const Datum *args, size_t argc) {
  return std_unary_trigonometry(interpreter, args, argc, "tan", tanf, false);
}

// (asin) — Inverse sine, returning radians
Datum std_asin(Interpreter *interpreter, const Datum *args, size_t argc) {
  return std_unary_trigonometry(interpreter, args, argc, "asin", asinf, true);
}

// (acos) — Inverse cosine, returning radians
Datum std_acos(Interpreter *interpreter, const Datum *args, size_t argc) {
  return std_unary_trigonometry(interpreter, args, argc, "acos", acosf, true);
}

// (atan) — Inverse tangent, returning radians
Datum std_atan(Interpreter *interpreter, const Datum *args, size_t argc) {
  return std_unary_trigonometry(interpreter, args, argc, "atan", atanf, false);
}

// (atan2 Y X) — Angle of a vector, returning radians
Datum std_atan2(Interpreter *interpreter, const Datum *args, size_t argc) {
  return std_binary_trigonometry(interpreter, args, argc, "atan2", atan2f);
}

// (hypot X Y) — Euclidean distance, sqrt(X*X + Y*Y)
Datum std_hypot(Interpreter *interpreter, const Datum *args, size_t argc) {
  return std_binary_trigonometry(interpreter, args, argc, "hypot", hypotf);
}
