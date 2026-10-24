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

Value std_print(const Value *args, size_t argc) {
  return std_generic_print(STD_PRINT, args, argc);
}

Value std_println(const Value *args, size_t argc) {
  return std_generic_print(STD_PRINTLN, args, argc);
}

Value std_eprint(const Value *args, size_t argc) {
  return std_generic_print(STD_EPRINT, args, argc);
}

Value std_eprintln(const Value *args, size_t argc) {
  return std_generic_print(STD_EPRINTLN, args, argc);
}

// Populate the `symbols` with “standard library” definitions
bool populate_stdlib(char (*error_message)[ERR_MSG_SIZE]) {
  const char *symbol_name;

  symbol_name = "print";
  Symbol *std_print_sym = symbol_define(symbol_name, SYM_NATIVE_FUNCTION);
  if (std_print_sym == NULL) goto symbol_define_fail;
  std_print_sym->as.native_function = std_print;

  symbol_name = "println";
  Symbol *std_println_sym = symbol_define(symbol_name, SYM_NATIVE_FUNCTION);
  if (std_println_sym == NULL) goto symbol_define_fail;
  std_println_sym->as.native_function = std_println;

  symbol_name = "eprint";
  Symbol *std_eprint_sym = symbol_define(symbol_name, SYM_NATIVE_FUNCTION);
  if (std_eprint_sym == NULL) goto symbol_define_fail;
  std_eprint_sym->as.native_function = std_eprint;

  symbol_name = "eprintln";
  Symbol *std_eprintln_sym = symbol_define(symbol_name, SYM_NATIVE_FUNCTION);
  if (std_eprintln_sym == NULL) goto symbol_define_fail;
  std_eprintln_sym->as.native_function = std_eprintln;

  return true;

  symbol_define_fail:
    extract_error_message_from_symbol_define_error(error_message, symbol_name);
    return false;
}
