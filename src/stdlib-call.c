#include "stdlib-call.h"
#include "stdlib-variables.h"
#include "stdlib-validation-utils.h"
#include "wipe-memory.h"

// (call FUNCTION-NAME ARG…) — Invoke a native function by its string name.
// All arguments have already been evaluated by the ordinary call machinery.
// Evaluator special forms are not symbols and cannot be invoked this way.
Datum std_call(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "call";
  char symbol_name[SYMBOL_NAME_SIZE] = {'\0'};

  if (argc == 0) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects at least 1 argument", fn_name);
    goto fail;
  }

  if (!get_variable_name(&args[0], &symbol_name)) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid function name", fn_name);
    goto fail;
  }

  const Symbol *symbol = symbol_lookup(interpreter, symbol_name);
  if (symbol == NULL) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) function “%s” is not defined", fn_name, symbol_name);
    goto fail;
  }
  if (symbol->kind != SYM_NATIVE_FUNCTION) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) symbol “%s” is not a native function",
      fn_name,
      symbol_name
    );
    goto fail;
  }

  // Preserve the caller’s native-call expression index for functions like
  // (label). The evaluator also keeps the evaluated arguments GC-rooted.
  return symbol->as.native_function(interpreter, args + 1, argc - 1);

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}
