#include <math.h>

#include "stdlib-arithmetics.h"
#include "stdlib-validation-utils.h"
#include "symbol-error.h"

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

#define ADD_ARITHMETIC_FN_TYPED(name, expr_name, u32_op, i32_op, f_op) \
  ADD_ARITHMETIC_FN_IMPL(name, expr_name, u32_op, i32_op, f_op, true)

#define ADD_ARITHMETIC_FN(name, expr_name, op) \
  ADD_ARITHMETIC_FN_TYPED(name, expr_name, op, op, op)

#define ADD_ARITHMETIC_FN_NO_ZERO_DIVISOR(name, expr_name, op) \
  ADD_ARITHMETIC_FN_IMPL( \
    name, expr_name, op, op, op, \
    validate_nonzero_divisor(fn_name, &fail_msg, args) \
  )

#define ADD_ARITHMETIC_FN_TYPED_NO_ZERO_DIVISOR(name, expr_name, u32_op, i32_op, f_op) \
  ADD_ARITHMETIC_FN_IMPL( \
    name, expr_name, u32_op, i32_op, f_op, \
    validate_nonzero_divisor(fn_name, &fail_msg, args) \
  )

ADD_ARITHMETIC_FN(std_add, "+", a + b)
ADD_ARITHMETIC_FN(std_subtract, "-", a - b)
ADD_ARITHMETIC_FN(std_mul, "*", a * b)
ADD_ARITHMETIC_FN_NO_ZERO_DIVISOR(std_divide, "/", a / b)

ADD_ARITHMETIC_FN_TYPED_NO_ZERO_DIVISOR(std_mod, "mod", a % b, a % b, fmodf(a, b))
ADD_ARITHMETIC_FN_TYPED_NO_ZERO_DIVISOR(std_div, "div", a / b, a / b, truncf(a / b))

// Unary rounding
ADD_UNARY_ROUNDING_FN(std_floor, "floor", floorf(x))
ADD_UNARY_ROUNDING_FN(std_ceiling, "ceiling", ceilf(x))
ADD_UNARY_ROUNDING_FN(std_round, "round", roundf(x))
ADD_UNARY_ROUNDING_FN(std_trunc, "trunc", truncf(x))

// Populate the `symbols` with “standard library” arithmetics functions
bool populate_stdlib_arithmetics(char (*error_message)[ERR_MSG_SIZE]) {
  const char *symbol_name;

  ADD_FN_SYMBOL("+", std_add);
  ADD_FN_SYMBOL("-", std_subtract);
  ADD_FN_SYMBOL("*", std_mul);
  ADD_FN_SYMBOL("/", std_divide);
  ADD_FN_SYMBOL("mod", std_mod);
  ADD_FN_SYMBOL("div", std_div);
  ADD_FN_SYMBOL("floor", std_floor);
  ADD_FN_SYMBOL("ceiling", std_ceiling);
  ADD_FN_SYMBOL("round", std_round);
  ADD_FN_SYMBOL("trunc", std_trunc);

  return true;

  symbol_define_fail:
    extract_error_message_from_symbol_define_error(error_message, symbol_name);
    return false;
}
