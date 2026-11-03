#include <math.h>

#include "stdlib-validation-utils.h"
#include "wipe-memory.h"
#include "symbol-print.h"

// Validate exact amount of arguments
bool validate_argc(
  Interpreter *interpreter,
  const char *fn_name,
  char (*fail_msg)[ERR_MSG_SIZE],
  size_t wanted_argc,
  size_t argc
) {
  if (wanted_argc == argc) return true;
  if (wanted_argc <= 0) {
    snprintf(
      *fail_msg,
      sizeof(*fail_msg),
      "(%s) does not take any arguments, got %zu argument%s",
      fn_name,
      argc,
      (argc == 1 ? "" : "s")
    );
  } else {
    snprintf(
      *fail_msg,
      sizeof(*fail_msg),
      "(%s) expects exactly %zu argument%s, got instead: %zu",
      fn_name,
      wanted_argc,
      (wanted_argc == 1 ? "" : "s"),
      argc
    );
  }
  report_evaluation_failure(interpreter, *fail_msg);
  wipe_memory(*fail_msg, sizeof(*fail_msg));
  return false;
}

// Report receiving unexpected value type
void report_unexpected_value_type(
  Interpreter *interpreter,
  const char *fn_name,
  char (*fail_msg)[ERR_MSG_SIZE],
  ValueType type
) {
  snprintf(
    *fail_msg,
    sizeof(*fail_msg),
    "(%s) received a value of an unexpected type: %s",
    fn_name,
    show_ValueType(type)
  );
  report_evaluation_failure(interpreter, *fail_msg);
  wipe_memory(*fail_msg, sizeof(*fail_msg));
}

// Validate arithmetic inputs and report failure if validation fails
bool validate_two_same_type_variables_inputs(
  Interpreter *interpreter,
  const char *fn_name,
  char (*fail_msg)[ERR_MSG_SIZE],
  const Datum *args,
  size_t argc
) {
  if (!validate_argc(interpreter, fn_name, fail_msg, 2, argc)) {
    return false;
  } else if (args[0].kind != SYM_VARIABLE || args[1].kind != SYM_VARIABLE) {
    snprintf(*fail_msg, sizeof(*fail_msg), "(%s) expects two values, not arrays", fn_name);
    report_evaluation_failure(interpreter, *fail_msg);
    wipe_memory(*fail_msg, sizeof(*fail_msg));
    return false;
  } else if (args[0].as.value.type != args[1].as.value.type) {
    snprintf(
      *fail_msg,
      sizeof(*fail_msg),
      "(%s) expects both arguments be of the same type, got instead: (%s, %s)",
      fn_name,
      show_ValueType(args[0].as.value.type),
      show_ValueType(args[1].as.value.type)
    );
    report_evaluation_failure(interpreter, *fail_msg);
    wipe_memory(*fail_msg, sizeof(*fail_msg));
    return false;
  } else {
    return true;
  }
}

// Assuming both arguments are already validated with `validate_two_same_type_variables_inputs`
bool validate_nonzero_divisor(
  Interpreter *interpreter,
  const char *fn_name,
  char (*fail_msg)[ERR_MSG_SIZE],
  const Datum *args
) {
  const Value *divisor = &args[1].as.value;
  bool is_zero = false;

  switch (divisor->type) {
    case TYPE_U32: is_zero = divisor->as.u32 == 0; break;
    case TYPE_I32: is_zero = divisor->as.i32 == 0; break;
    case TYPE_F32: is_zero = divisor->as.f32 == 0.0f; break;
    default: return true; // Existing arithmetic switch handles unsupported types
  }

  if (!is_zero) return true;

  snprintf(*fail_msg, sizeof(*fail_msg), "(%s) division by zero", fn_name);
  report_evaluation_failure(interpreter, *fail_msg);
  wipe_memory(*fail_msg, sizeof(*fail_msg));
  return false;
}

// Validate unary numeric input and report failure if validation fails
bool validate_unary_numeric_input(
  Interpreter *interpreter,
  const char *fn_name,
  char (*fail_msg)[ERR_MSG_SIZE],
  const Datum *args,
  size_t argc
) {
  if (!validate_argc(interpreter, fn_name, fail_msg, 1, argc)) {
    return false;
  } else if (args[0].kind != SYM_VARIABLE) {
    snprintf(
      *fail_msg,
      sizeof(*fail_msg),
      "(%s) expects a numeric value, not an array",
      fn_name
    );
    report_evaluation_failure(interpreter, *fail_msg);
    wipe_memory(*fail_msg, sizeof(*fail_msg));
    return false;
  } else {
    switch (args[0].as.value.type) {
      case TYPE_U32:
      case TYPE_I32:
      case TYPE_F32:
        return true;
      default:
        report_unexpected_value_type(interpreter, fn_name, fail_msg, args[0].as.value.type);
        return false;
    }
  }
}

// Apply a unary f32 mathematical function and reject non-finite values
Datum std_unary_f32_math(
  Interpreter *interpreter,
  const Datum *args,
  size_t argc,
  const char *fn_name,
  float (*operation)(float)
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
