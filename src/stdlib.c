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

// (+)
Datum std_add(const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  char fn_name[] = "+";
  if (!validate_arithmetic_inputs(fn_name, &fail_msg, args, argc)) return return_value;

  switch (args[0].as.value.type) {
    case TYPE_U32:
      return_value.as.value.as.u32 = args[0].as.value.as.u32 + args[1].as.value.as.u32;
      break;
    case TYPE_I32:
      return_value.as.value.as.i32 = args[0].as.value.as.i32 + args[1].as.value.as.i32;
      break;
    case TYPE_F32:
      return_value.as.value.as.f32 = args[0].as.value.as.f32 + args[1].as.value.as.f32;
      break;
    default:
      report_unexpected_value_type(fn_name, &fail_msg, args[0].as.value.type);
      return return_value;
  }

  return_value.kind = SYM_VARIABLE;
  return_value.as.value.type = args[0].as.value.type;
  return return_value;
}

#define ADD_SYMBOL(name, fn) { \
  symbol_name = name; \
  Symbol *sym = symbol_define(symbol_name, SYM_NATIVE_FUNCTION); \
  if (sym == NULL) goto symbol_define_fail; \
  sym->as.native_function = fn; \
}

// Populate the `symbols` with “standard library” definitions
bool populate_stdlib(char (*error_message)[ERR_MSG_SIZE]) {
  const char *symbol_name;

  // Value accessing
  ADD_SYMBOL("last-val", std_last_val);

  // Printing
  ADD_SYMBOL("print", std_print);
  ADD_SYMBOL("println", std_println);
  ADD_SYMBOL("eprint", std_eprint);
  ADD_SYMBOL("eprintln", std_eprintln);

  // Arithmetics
  ADD_SYMBOL("+", std_add);

  return true;

  symbol_define_fail:
    extract_error_message_from_symbol_define_error(error_message, symbol_name);
    return false;
}
