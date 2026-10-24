#include "stdlib.h"
#include "symbol-error.h"
#include "symbol-print.h"
#include "wipe-memory.h"
#include "evaluate.h"
#include "stdlib-print.h"

// Generic standard library printing implementation
Value std_generic_print(StdPrintVariant variant, const Value *args, size_t argc) {
  Value return_value = { .type = TYPE_VOID };
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
  } else if (args[0].type != TYPE_STRING) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "%s argument must be a string, got instead: %s",
      show_StdPrintVariant(variant),
      show_ValueType(args[0].type)
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
    args[0].as.string.data,
    sizeof(char),
    args[0].as.string.length,
    stream
  );

  if (variant == STD_PRINTLN || variant == STD_EPRINTLN) fputc('\n', stream);

  return return_value;
}

// (print)
Value std_print(const Value *args, size_t argc) {
  return std_generic_print(STD_PRINT, args, argc);
}

// (println)
Value std_println(const Value *args, size_t argc) {
  return std_generic_print(STD_PRINTLN, args, argc);
}

// (eprint)
Value std_eprint(const Value *args, size_t argc) {
  return std_generic_print(STD_EPRINT, args, argc);
}

// (eprintln)
Value std_eprintln(const Value *args, size_t argc) {
  return std_generic_print(STD_EPRINTLN, args, argc);
}

// (+)
Value std_add(const Value *args, size_t argc) {
  Value return_value = { .type = TYPE_VOID };
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
  } else if (args[0].type != args[1].type) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(+) expects both arguments be of the same type, got instead: (%s, %s)",
      show_ValueType(args[0].type),
      show_ValueType(args[1].type)
    );
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
  }

  switch (args[0].type) {
    case TYPE_U32: return_value.as.u32 = args[0].as.u32 + args[1].as.u32; break;
    case TYPE_I32: return_value.as.i32 = args[0].as.i32 + args[1].as.i32; break;
    case TYPE_F32: return_value.as.f32 = args[0].as.f32 + args[1].as.f32; break;
    default:
      snprintf(
        fail_msg,
        sizeof(fail_msg),
        "(+) received a value of an unexpected type: %s",
        show_ValueType(args[0].type)
      );
      report_evaluation_failure(fail_msg);
      wipe_memory(fail_msg, sizeof(fail_msg));
      return return_value;
  }

  return_value.type = args[0].type;
  return return_value;
}

// (last-val) — Read last evaluation result in a sequence of expressions
Value std_last_val(const Value*, size_t argc) {
  Value return_value = { .type = TYPE_VOID };
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
