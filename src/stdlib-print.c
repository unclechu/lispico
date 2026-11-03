#include "stdlib-print.h"
#include "stdlib-validation-utils.h"
#include "symbol-print.h"
#include "evaluate.h"
#include "wipe-memory.h"
#include "symbol-error.h"

// Get a string representation of `show_StdPrintVariant` value
const char* show_StdPrintVariant(StdPrintVariant variant) {
  switch (variant) {
    case STD_PRINT: return "print";
    case STD_PRINTLN: return "println";
    case STD_EPRINT: return "eprint";
    case STD_EPRINTLN: return "eprintln";
    default: return "( ! Unexpected StdPrintVariant for show_StdPrintVariant ! )";
  }
}

// Generic standard library printing implementation
Datum std_generic_print(
  Interpreter *interpreter,
  StdPrintVariant variant,
  const Datum *args,
  size_t argc
) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char *fn_name = show_StdPrintVariant(variant);

  if (!validate_argc(interpreter, fn_name, &fail_msg, 1, argc)) {
    return return_value;
  } else if (args[0].kind != SYM_VARIABLE) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) argument must be a varaible, got instead: %s",
      fn_name,
      show_SymbolKind(args[0].kind)
    );
    report_evaluation_failure(interpreter, fail_msg);
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
    report_evaluation_failure(interpreter, fail_msg);
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
Datum std_print(Interpreter *interpreter, const Datum *args, size_t argc) {
  return std_generic_print(interpreter, STD_PRINT, args, argc);
}

// (println)
Datum std_println(Interpreter *interpreter, const Datum *args, size_t argc) {
  return std_generic_print(interpreter, STD_PRINTLN, args, argc);
}

// (eprint)
Datum std_eprint(Interpreter *interpreter, const Datum *args, size_t argc) {
  return std_generic_print(interpreter, STD_EPRINT, args, argc);
}

// (eprintln)
Datum std_eprintln(Interpreter *interpreter, const Datum *args, size_t argc) {
  return std_generic_print(interpreter, STD_EPRINTLN, args, argc);
}
