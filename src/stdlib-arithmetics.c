#include <math.h>

#include "stdlib-arithmetics.h"
#include "stdlib-validation-utils.h"
#include "symbol-error.h"
#include "evaluate.h"
#include "wipe-memory.h"

#define ADD_ARITHMETIC_FN_IMPL(name, expr_name, u32_op, i32_op, f_op, check) \
Datum name(const Datum *args, size_t argc) { \
  Datum return_value = { .kind = SYM_EMPTY }; \
  char fail_msg[ERR_MSG_SIZE] = {'\0'}; \
  char fn_name[] = expr_name; \
  if (!validate_two_same_type_variables_inputs(fn_name, &fail_msg, args, argc)) return return_value; \
  if (!(check)) return return_value; \
  switch (args[0].as.value.type) { \
    case TYPE_U32: { \
      uint32_t a = args[0].as.value.as.u32, b = args[1].as.value.as.u32; \
      return_value.as.value.as.u32 = u32_op; \
      break; \
    } \
    case TYPE_I32: { \
      int32_t a = args[0].as.value.as.i32, b = args[1].as.value.as.i32; \
      return_value.as.value.as.i32 = i32_op; \
      break; \
    } \
    case TYPE_F32: { \
      float a = args[0].as.value.as.f32, b = args[1].as.value.as.f32; \
      return_value.as.value.as.f32 = f_op; \
      break; \
    } \
    default: \
      report_unexpected_value_type(fn_name, &fail_msg, args[0].as.value.type); \
      return return_value; \
  } \
  return_value.kind = SYM_VARIABLE; \
  return_value.as.value.type = args[0].as.value.type; \
  return return_value; \
}

#define ADD_UNARY_ROUNDING_FN(name, expr_name, f_op) \
Datum name(const Datum *args, size_t argc) { \
  Datum return_value = { .kind = SYM_EMPTY }; \
  char fail_msg[ERR_MSG_SIZE] = {'\0'}; \
  if (!validate_unary_numeric_input(expr_name, &fail_msg, args, argc)) \
    return return_value; \
  return_value.kind = SYM_VARIABLE; \
  return_value.as.value.type = args[0].as.value.type; \
  if (args[0].as.value.type == TYPE_F32) { \
    float x = args[0].as.value.as.f32; \
    return_value.as.value.as.f32 = f_op; \
  } else { \
    return_value.as.value = args[0].as.value; \
  } \
  return return_value; \
}

#define ADD_ARITHMETIC_FN_TYPED_NO_ZERO_DIVISOR(name, expr_name, u32_op, i32_op, f_op) \
  ADD_ARITHMETIC_FN_IMPL( \
    name, expr_name, u32_op, i32_op, f_op, \
    validate_nonzero_divisor(fn_name, &fail_msg, args) \
  )

typedef enum {
  ARITH_ADD = 0,
  ARITH_SUBTRACT,
  ARITH_MULTIPLY,
  ARITH_DIVIDE,
  ARITH_DIV,
} ArithmeticOperation;

// Apply an arithmetic operation to two or more arguments from left to right.
static Datum std_variadic_arithmetic(
  const Datum *args,
  size_t argc,
  const char *fn_name,
  ArithmeticOperation operation
) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};

  if (argc < 2) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects at least 2 arguments", fn_name);
    goto fail;
  }

  if (args[0].kind != SYM_VARIABLE) goto invalid_value;

  Value value = args[0].as.value;

  switch (value.type) {
    case TYPE_U32:
    case TYPE_I32:
    case TYPE_F32:
      break;
    default:
      goto invalid_value;
  }

  // Accumulate the result, using each argument in order.
  for (size_t i = 1; i < argc; i++) {
    if (args[i].kind != SYM_VARIABLE) goto invalid_value;

    Value current = args[i].as.value;

    if (current.type != value.type) {
      snprintf(
        fail_msg,
        sizeof(fail_msg),
        "(%s) expects all arguments to be of the same type",
        fn_name
      );
      goto fail;
    }

    // Check every divisor, not just the second argument.
    if (operation == ARITH_DIVIDE || operation == ARITH_DIV) {
      bool is_zero = false;

      switch (value.type) {
        case TYPE_U32: is_zero = current.as.u32 == 0; break;
        case TYPE_I32: is_zero = current.as.i32 == 0; break;
        case TYPE_F32: is_zero = current.as.f32 == 0.0f; break;
        default: goto invalid_value;
      }

      if (is_zero) goto division_by_zero;
    }

    switch (value.type) {
      case TYPE_U32: {
        uint32_t a = value.as.u32;
        uint32_t b = current.as.u32;

        // Unsigned arithmetic wraps modulo 2^32.
        switch (operation) {
          case ARITH_ADD: value.as.u32 = a + b; break;
          case ARITH_SUBTRACT: value.as.u32 = a - b; break;
          case ARITH_MULTIPLY: value.as.u32 = a * b; break;

          case ARITH_DIVIDE:
          case ARITH_DIV:
            value.as.u32 = a / b;
            break;
        }
        break;
      }

      case TYPE_I32: {
        // Use a wider integer to avoid overflowing i32 during calculations.
        int64_t a = value.as.i32;
        int64_t b = current.as.i32;
        int64_t result = 0;

        switch (operation) {
          case ARITH_ADD: result = a + b; break;
          case ARITH_SUBTRACT: result = a - b; break;
          case ARITH_MULTIPLY: result = a * b; break;

          case ARITH_DIVIDE:
          case ARITH_DIV:
            result = a / b;
            break;
        }

        if (result < INT32_MIN || result > INT32_MAX) goto out_of_range;
        value.as.i32 = (int32_t)result;
        break;
      }

      case TYPE_F32: {
        float a = value.as.f32;
        float b = current.as.f32;

        switch (operation) {
          case ARITH_ADD: value.as.f32 = a + b; break;
          case ARITH_SUBTRACT: value.as.f32 = a - b; break;
          case ARITH_MULTIPLY: value.as.f32 = a * b; break;
          case ARITH_DIVIDE: value.as.f32 = a / b; break;
          case ARITH_DIV: value.as.f32 = truncf(a / b); break;
        }
        break;
      }

      default: goto invalid_value;
    }
  }

  return_value = (Datum) { .kind = SYM_VARIABLE, .as.value = value };
  return return_value;

  invalid_value:
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects numeric values (u32, i32, or f32)", fn_name);
    goto fail;

  division_by_zero:
    snprintf(fail_msg, sizeof(fail_msg), "(%s) division by zero", fn_name);
    goto fail;

  out_of_range:
    snprintf(fail_msg, sizeof(fail_msg), "(%s) result exceeds i32 range", fn_name);
    goto fail;

  fail:
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (+) — Add two or more values
Datum std_add(const Datum *args, size_t argc) {
  return std_variadic_arithmetic(args, argc, "+", ARITH_ADD);
}

// (-) — Subtract two or more values
Datum std_subtract(const Datum *args, size_t argc) {
  return std_variadic_arithmetic(args, argc, "-", ARITH_SUBTRACT);
}

// (*) — Multiply two or more values
Datum std_mul(const Datum *args, size_t argc) {
  return std_variadic_arithmetic(args, argc, "*", ARITH_MULTIPLY);
}

// (/) — Divide two or more values
Datum std_divide(const Datum *args, size_t argc) {
  return std_variadic_arithmetic(args, argc, "/", ARITH_DIVIDE);
}

// (div) — Divide two or more values, truncating f32 results
Datum std_div(const Datum *args, size_t argc) {
  return std_variadic_arithmetic(args, argc, "div", ARITH_DIV);
}

ADD_ARITHMETIC_FN_TYPED_NO_ZERO_DIVISOR(std_mod, "mod", a % b, a % b, fmodf(a, b))

// Unary rounding
ADD_UNARY_ROUNDING_FN(std_floor, "floor", floorf(x))
ADD_UNARY_ROUNDING_FN(std_ceiling, "ceiling", ceilf(x))
ADD_UNARY_ROUNDING_FN(std_round, "round", roundf(x))
ADD_UNARY_ROUNDING_FN(std_trunc, "trunc", truncf(x))
