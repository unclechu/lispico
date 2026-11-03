#include <string.h>

#include "stdlib-comparisons.h"
#include "stdlib-validation-utils.h"
#include "symbol-error.h"

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
Datum name(Interpreter *interpreter, const Datum *args, size_t argc) { \
  Datum return_value = { .kind = SYM_EMPTY }; \
  char fail_msg[ERR_MSG_SIZE] = {'\0'}; \
  const char fn_name[] = expr_name; \
  if (!validate_two_same_type_variables_inputs(interpreter, fn_name, &fail_msg, args, argc)) \
    return return_value; \
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
      report_unexpected_value_type(interpreter, fn_name, &fail_msg, args[0].as.value.type); \
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
