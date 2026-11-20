#include "stdlib-call.h"
#include "stdlib-variables.h"
#include "stdlib-validation-utils.h"
#include "wipe-memory.h"
#include "evaluate.h"

// (call FUNCTION-NAME ARG…) — Invoke a user-defined or native function by name.
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

  char function_label[SYMBOL_NAME_SIZE] = {'\0'};
  int label_length = snprintf(
    function_label,
    sizeof(function_label),
    "_FN_%s",
    symbol_name
  );

  if (label_length > 0 && (size_t)label_length < sizeof(function_label)) {
    const Symbol *function = symbol_lookup(interpreter, function_label);
    if (function != NULL && function->kind == SYM_LABEL) {
      if (!evaluate_user_function(
        interpreter,
        function->as.instruction_index,
        // Skip the function name (first argument), passing only the function arguments.
        // +1 for the pointer shifting and -1 for the argument count.
        args + 1,
        argc - 1,
        &return_value
      )) return return_value;
      return return_value;
    }
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
