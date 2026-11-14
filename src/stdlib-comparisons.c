#include <string.h>

#include "stdlib-comparisons.h"
#include "stdlib-validation-utils.h"
#include "symbol-error.h"
#include "symbol-print.h"

static int compare_strings(String a, String b) {
  size_t min_length = a.length < b.length ? a.length : b.length;
  int comparison = 0;
  if (min_length != 0) comparison = memcmp(a.data, b.data, min_length);
  if (comparison != 0) return comparison;
  if (a.length < b.length) return -1;
  if (a.length > b.length) return 1;
  return 0;
}

static bool arrays_equal(Array a, Array b) {
  if (a.length != b.length) return false;

  for (uint32_t i = 0; i < a.length; ++i) {
    switch (a.element_type) {
      case TYPE_U32:
        if (((uint32_t *)a.data)[i] != ((uint32_t *)b.data)[i]) return false;
        break;
      case TYPE_I32:
        if (((int32_t *)a.data)[i] != ((int32_t *)b.data)[i]) return false;
        break;
      case TYPE_F32:
        if (((float *)a.data)[i] != ((float *)b.data)[i]) return false;
        break;
      case TYPE_STRING:
        if (compare_strings(((String *)a.data)[i], ((String *)b.data)[i]) != 0) return false;
        break;
      default:
        return false;
    }
  }

  return true;
}

#define ADD_COMPARISON_FN(name, expr_name, op, compare_arrays) \
Datum name(Interpreter *interpreter, const Datum *args, size_t argc) { \
  Datum return_value = { .kind = SYM_EMPTY }; \
  char fail_msg[ERR_MSG_SIZE] = {'\0'}; \
  const char fn_name[] = expr_name; \
  if (!validate_argc(interpreter, fn_name, &fail_msg, 2, argc)) return return_value; \
  if ( \
    compare_arrays && \
    args[0].kind == SYM_ARRAY && \
    args[1].kind == SYM_ARRAY \
  ) { \
    Array a = args[0].as.array, b = args[1].as.array; \
    if (a.element_type != b.element_type) { \
      snprintf( \
        fail_msg, \
        sizeof(fail_msg), \
        "(%s) expects both arrays to have the same element type, got instead: (%s, %s)", \
        fn_name, \
        show_ValueType(a.element_type), \
        show_ValueType(b.element_type) \
      ); \
      report_evaluation_failure(interpreter, fail_msg); \
      return return_value; \
    } \
    if ( \
      a.element_type != TYPE_U32 && \
      a.element_type != TYPE_I32 && \
      a.element_type != TYPE_F32 && \
      a.element_type != TYPE_STRING \
    ) { \
      report_unexpected_value_type(interpreter, fn_name, &fail_msg, a.element_type); \
      return return_value; \
    } \
    return_value.kind = SYM_VARIABLE; \
    return_value.as.value.type = TYPE_U32; \
    return_value.as.value.as.u32 = arrays_equal(a, b) op true; \
    return return_value; \
  } \
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

ADD_COMPARISON_FN(std_less, "<", <, false)
ADD_COMPARISON_FN(std_less_equal, "<=", <=, false)
ADD_COMPARISON_FN(std_greater, ">", >, false)
ADD_COMPARISON_FN(std_greater_equal, ">=", >=, false)
ADD_COMPARISON_FN(std_equal, "==", ==, true)
ADD_COMPARISON_FN(std_not_equal, "!=", !=, true)
