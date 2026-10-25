#include "stdlib.h"
#include "symbol-error.h"
#include "symbol-print.h"
#include "wipe-memory.h"
#include "evaluate.h"
#include "stdlib-print.h"

// Generic standard library printing implementation
Datum std_generic_print(StdPrintVariant variant, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};

  if (argc != 1) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "%s expects exactly 1 argument, got instead: %zu",
      show_StdPrintVariant(variant),
      argc
    );
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
  } else if (args[0].kind != SYM_VARIABLE) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "%s argument must be a varaible, got instead: %s",
      show_StdPrintVariant(variant),
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
      show_StdPrintVariant(variant),
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

// (+)
Datum std_add(const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};

  if (argc != 2) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(+) expects exactly 2 arguments, got instead: %zu",
      argc
    );
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
  } else if (args[0].kind != SYM_VARIABLE || args[1].kind != SYM_VARIABLE) {
    report_evaluation_failure("(+) expects two values, not arrays");
    return return_value;
  } else if (args[0].as.value.type != args[1].as.value.type) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(+) expects both arguments be of the same type, got instead: (%s, %s)",
      show_ValueType(args[0].as.value.type),
      show_ValueType(args[1].as.value.type)
    );
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
  }

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
      snprintf(
        fail_msg,
        sizeof(fail_msg),
        "(+) received a value of an unexpected type: %s",
        show_ValueType(args[0].as.value.type)
      );
      report_evaluation_failure(fail_msg);
      wipe_memory(fail_msg, sizeof(fail_msg));
      return return_value;
  }

  return_value.kind = SYM_VARIABLE;
  return_value.as.value.type = args[0].as.value.type;
  return return_value;
}

// (last-val) — Read last evaluation result in a sequence of expressions
Datum std_last_val(const Datum*, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};

  if (argc != 0) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(last-val) does not take any arguments, got %zu arguments",
      argc
    );
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
  }

  if (!get_last_evaluation_value(&return_value)) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(last-val) no previous value is available"
    );
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
  }

  return return_value;
}

// Populate the `symbols` with “standard library” definitions
bool populate_stdlib(char (*error_message)[ERR_MSG_SIZE]) {
  const char *symbol_name;

  {
    symbol_name = "print";
    Symbol *sym = symbol_define(symbol_name, SYM_NATIVE_FUNCTION);
    if (sym == NULL) goto symbol_define_fail;
    sym->as.native_function = std_print;
  }

  {
    symbol_name = "println";
    Symbol *sym = symbol_define(symbol_name, SYM_NATIVE_FUNCTION);
    if (sym == NULL) goto symbol_define_fail;
    sym->as.native_function = std_println;
  }

  {
    symbol_name = "eprint";
    Symbol *sym = symbol_define(symbol_name, SYM_NATIVE_FUNCTION);
    if (sym == NULL) goto symbol_define_fail;
    sym->as.native_function = std_eprint;
  }

  {
    symbol_name = "eprintln";
    Symbol *sym = symbol_define(symbol_name, SYM_NATIVE_FUNCTION);
    if (sym == NULL) goto symbol_define_fail;
    sym->as.native_function = std_eprintln;
  }

  {
    symbol_name = "+";
    Symbol *sym = symbol_define(symbol_name, SYM_NATIVE_FUNCTION);
    if (sym == NULL) goto symbol_define_fail;
    sym->as.native_function = std_add;
  }

  {
    symbol_name = "last-val";
    Symbol *sym = symbol_define(symbol_name, SYM_NATIVE_FUNCTION);
    if (sym == NULL) goto symbol_define_fail;
    sym->as.native_function = std_last_val;
  }

  return true;

  symbol_define_fail:
    extract_error_message_from_symbol_define_error(error_message, symbol_name);
    return false;
}
