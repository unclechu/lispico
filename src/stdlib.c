#include <math.h>
#include <inttypes.h>
#include <string.h>
#include <limits.h>

#include "stdlib.h"
#include "symbol-error.h"
#include "symbol-print.h"
#include "wipe-memory.h"
#include "evaluate.h"
#include "stdlib-print.h"

static void report_unexpected_value_type(
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
  report_evaluation_failure(*fail_msg);
  wipe_memory(*fail_msg, sizeof(*fail_msg));
}

static bool validate_argc(
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
  report_evaluation_failure(*fail_msg);
  wipe_memory(*fail_msg, sizeof(*fail_msg));
  return false;
}

// (last-val) — Read last evaluation result in a sequence of expressions
Datum std_last_val(const Datum*, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  char fn_name[] = "last-val";

  if (!validate_argc(fn_name, &fail_msg, 0, argc)) return return_value;

  if (!get_last_evaluation_value(&return_value)) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) no previous value is available",
      fn_name
    );
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
  }

  return return_value;
}

#define ADD_TYPE_FN(name, expr_name, value_type) \
Datum name(const Datum*, size_t argc) { \
  Datum return_value = { .kind = SYM_EMPTY }; \
  char fail_msg[ERR_MSG_SIZE] = {'\0'}; \
  if (!validate_argc(expr_name, &fail_msg, 0, argc)) return return_value; \
  return_value.kind = SYM_VARIABLE; \
  return_value.as.value.type = TYPE_U32; \
  return_value.as.value.as.u32 = value_type; \
  return return_value; \
}

ADD_TYPE_FN(std_uint32, "uint32", TYPE_U32)
ADD_TYPE_FN(std_int32, "int32", TYPE_I32)
ADD_TYPE_FN(std_float, "float", TYPE_F32)

// (type) — Explicit numeric type conversion
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
      "(%s) expects a uint32 type identifier and a scalar value",
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
    default:
      snprintf(
        fail_msg, sizeof(fail_msg),
        "(%s) unknown target type identifier: %" PRIu32,
        fn_name,
        type_id
      );
      goto fail;
  }

  Value converted = { .type = target_type };

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

    default:
      goto unsupported;
  }

  return_value = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = converted,
  };

  return return_value;

  out_of_range:
    snprintf(
      fail_msg, sizeof(fail_msg),
      "(%s) value is out of range for %s",
      fn_name,
      show_ValueType(target_type)
    );
    goto fail;

  unsupported:
    snprintf(
      fail_msg, sizeof(fail_msg),
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

// Generic standard library printing implementation
Datum std_generic_print(StdPrintVariant variant, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char *fn_name = show_StdPrintVariant(variant);

  if (!validate_argc(fn_name, &fail_msg, 1, argc)) {
    return return_value;
  } else if (args[0].kind != SYM_VARIABLE) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) argument must be a varaible, got instead: %s",
      fn_name,
      show_SymbolKind(args[0].kind)
    );
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
  } else if (args[0].as.value.type != TYPE_STRING) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "%s argument must be a string, got instead: %s",
      fn_name,
      show_ValueType(args[0].as.value.type)
    );
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
  }

  FILE *stream =
    (variant == STD_EPRINT || variant == STD_EPRINTLN)
      ? stderr
      : stdout;

  fwrite(
    args[0].as.value.as.string.data,
    sizeof(char),
    args[0].as.value.as.string.length,
    stream
  );

  if (variant == STD_PRINTLN || variant == STD_EPRINTLN) fputc('\n', stream);

  return return_value;
}

// (print)
Datum std_print(const Datum *args, size_t argc) {
  return std_generic_print(STD_PRINT, args, argc);
}

// (println)
Datum std_println(const Datum *args, size_t argc) {
  return std_generic_print(STD_PRINTLN, args, argc);
}

// (eprint)
Datum std_eprint(const Datum *args, size_t argc) {
  return std_generic_print(STD_EPRINT, args, argc);
}

// (eprintln)
Datum std_eprintln(const Datum *args, size_t argc) {
  return std_generic_print(STD_EPRINTLN, args, argc);
}

// Validate arithmetic inputs and report failure if validation fails
static bool validate_arithmetic_inputs(
  const char *fn_name,
  char (*fail_msg)[ERR_MSG_SIZE],
  const Datum *args,
  size_t argc
) {
  if (!validate_argc(fn_name, fail_msg, 2, argc)) {
    return false;
  } else if (args[0].kind != SYM_VARIABLE || args[1].kind != SYM_VARIABLE) {
    snprintf(*fail_msg, sizeof(*fail_msg), "(%s) expects two values, not arrays", fn_name);
    report_evaluation_failure(*fail_msg);
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
    report_evaluation_failure(*fail_msg);
    wipe_memory(*fail_msg, sizeof(*fail_msg));
    return false;
  } else {
    return true;
  }
}

// Validate unary numeric input and report failure if validation fails
static bool validate_unary_numeric_input(
  const char *fn_name,
  char (*fail_msg)[ERR_MSG_SIZE],
  const Datum *args,
  size_t argc
) {
  if (!validate_argc(fn_name, fail_msg, 1, argc)) {
    return false;
  } else if (args[0].kind != SYM_VARIABLE) {
    snprintf(
      *fail_msg,
      sizeof(*fail_msg),
      "(%s) expects a numeric value, not an array",
      fn_name
    );
    report_evaluation_failure(*fail_msg);
    wipe_memory(*fail_msg, sizeof(*fail_msg));
    return false;
  } else {
    switch (args[0].as.value.type) {
      case TYPE_U32:
      case TYPE_I32:
      case TYPE_F32:
        return true;
      default:
        report_unexpected_value_type(fn_name, fail_msg, args[0].as.value.type);
        return false;
    }
  }
}

// Assuming both arguments are already validated with `validate_arithmetic_inputs`
static bool validate_nonzero_divisor(
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
  report_evaluation_failure(*fail_msg);
  wipe_memory(*fail_msg, sizeof(*fail_msg));
  return false;
}

#define ADD_ARITHMETIC_FN_IMPL(name, expr_name, u32_op, i32_op, f_op, check) \
Datum name(const Datum *args, size_t argc) { \
  Datum return_value = { .kind = SYM_EMPTY }; \
  char fail_msg[ERR_MSG_SIZE] = {'\0'}; \
  char fn_name[] = expr_name; \
  if (!validate_arithmetic_inputs(fn_name, &fail_msg, args, argc)) return return_value; \
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

#define ADD_FN_SYMBOL(name, fn) { \
  symbol_name = name; \
  Symbol *sym = symbol_define(symbol_name, SYM_NATIVE_FUNCTION); \
  if (sym == NULL) goto symbol_define_fail; \
  sym->as.native_function = fn; \
}

#define ADD_VAR_SYMBOL(name, type_id, as_type, val) { \
  symbol_name = name; \
  Symbol *sym = symbol_define(symbol_name, SYM_VARIABLE); \
  if (sym == NULL) goto symbol_define_fail; \
  sym->as.variable = (Value) { \
    .type = type_id, \
    .as.as_type = val, \
  }; \
}

static int compare_strings(String a, String b) {
  size_t min_length = a.length < b.length ? a.length : b.length;
  int comparison = 0;
  if (min_length != 0) comparison = memcmp(a.data, b.data, min_length);
  if (comparison != 0) return comparison;
  if (a.length < b.length) return -1;
  if (a.length > b.length) return 1;
  return 0;
}

#define ADD_COMPARISON_FN(name, expr_name, op) \
Datum name(const Datum *args, size_t argc) { \
  Datum return_value = { .kind = SYM_EMPTY }; \
  char fail_msg[ERR_MSG_SIZE] = {'\0'}; \
  const char fn_name[] = expr_name; \
  if (!validate_arithmetic_inputs(fn_name, &fail_msg, args, argc)) return return_value; \
  uint32_t comparison_result; \
  switch (args[0].as.value.type) { \
    case TYPE_U32: { \
      uint32_t a = args[0].as.value.as.u32, b = args[1].as.value.as.u32; \
      comparison_result = a op b; \
      break; \
    } \
    case TYPE_I32: { \
      int32_t a = args[0].as.value.as.i32, b = args[1].as.value.as.i32; \
      comparison_result = a op b; \
      break; \
    } \
    case TYPE_F32: { \
      float a = args[0].as.value.as.f32, b = args[1].as.value.as.f32; \
      comparison_result = a op b; \
      break; \
    } \
    case TYPE_STRING: { \
      String a = args[0].as.value.as.string; \
      String b = args[1].as.value.as.string; \
      int comparison = compare_strings(a, b); \
      comparison_result = comparison op 0; \
      break; \
    } \
    default: \
      report_unexpected_value_type(fn_name, &fail_msg, args[0].as.value.type); \
      return return_value; \
  } \
  return_value.kind = SYM_VARIABLE; \
  return_value.as.value.type = TYPE_U32; \
  return_value.as.value.as.u32 = comparison_result; \
  return return_value; \
}

ADD_COMPARISON_FN(std_less, "<", <)
ADD_COMPARISON_FN(std_less_equal, "<=", <=)
ADD_COMPARISON_FN(std_greater, ">", >)
ADD_COMPARISON_FN(std_greater_equal, ">=", >=)
ADD_COMPARISON_FN(std_equal, "==", ==)
ADD_COMPARISON_FN(std_not_equal, "!=", !=)

// (exit) — Stop program evaluation and return the provided result
Datum std_exit(const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "exit";

  if (!validate_argc(fn_name, &fail_msg, 2, argc)) return return_value;

  if (args[0].kind != SYM_VARIABLE) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) exit code must be a numeric value", fn_name);
    goto fail;
  }

  int exit_code;

  switch (args[0].as.value.type) {
    case TYPE_U32:
      if (args[0].as.value.as.u32 > INT_MAX) goto out_of_range;
      exit_code = (int)args[0].as.value.as.u32;
      break;

    case TYPE_I32:
      exit_code = (int)args[0].as.value.as.i32;
      break;

    default:
      snprintf(fail_msg, sizeof(fail_msg), "(%s) exit code must be uint32 or int32", fn_name);
      goto fail;
  }

  return_value = args[1];
  report_evaluation_exit(exit_code, return_value);
  return return_value;

  out_of_range:
    snprintf(fail_msg, sizeof(fail_msg), "(%s) exit code is out of range for int", fn_name);
    goto fail;

  fail:
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// Populate the `symbols` with “standard library” definitions
bool populate_stdlib(char (*error_message)[ERR_MSG_SIZE]) {
  const char *symbol_name;

  // Value accessing
  ADD_FN_SYMBOL("last-val", std_last_val);

  // Type identifiers
  ADD_VAR_SYMBOL("uint32", TYPE_U32, u32, TYPE_U32);
  ADD_VAR_SYMBOL("int32", TYPE_U32, u32, TYPE_I32);
  ADD_VAR_SYMBOL("float", TYPE_U32, u32, TYPE_F32);

  // Type conversion
  ADD_FN_SYMBOL("type", std_type);

  // Printing
  ADD_FN_SYMBOL("print", std_print);
  ADD_FN_SYMBOL("println", std_println);
  ADD_FN_SYMBOL("eprint", std_eprint);
  ADD_FN_SYMBOL("eprintln", std_eprintln);

  // Arithmetics
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

  // Boolean constants
  ADD_VAR_SYMBOL("true", TYPE_U32, u32, 1u);
  ADD_VAR_SYMBOL("false", TYPE_U32, u32, 0u);

  // Comparisons
  ADD_FN_SYMBOL("<", std_less);
  ADD_FN_SYMBOL("<=", std_less_equal);
  ADD_FN_SYMBOL(">", std_greater);
  ADD_FN_SYMBOL(">=", std_greater_equal);
  ADD_FN_SYMBOL("==", std_equal);
  ADD_FN_SYMBOL("!=", std_not_equal);

  ADD_FN_SYMBOL("exit", std_exit);

  return true;

  symbol_define_fail:
    extract_error_message_from_symbol_define_error(error_message, symbol_name);
    return false;
}
