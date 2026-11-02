#include "stdlib-jumps.h"
#include "stdlib-variables.h"
#include "stdlib-validation-utils.h"
#include "symbol-error.h"
#include "wipe-memory.h"
#include "evaluate.h"
#include "storage.h"

// (label) — Define or update a global label at the current expression
Datum std_label(const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "label";
  char symbol_name[SYMBOL_NAME_SIZE] = {'\0'};

  if (!validate_argc(fn_name, &fail_msg, 1, argc)) return return_value;

  if (!get_variable_name(&args[0], &symbol_name)) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid label name", fn_name);
    goto fail;
  }

  uint32_t expression_index = get_current_native_call_expression_index();

  if (expression_index == EXPR_NONE || expression_index >= expression_count) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid expression location", fn_name);
    goto fail;
  }

  // Check first if such label already exists
  // (and just overwrite it, replace the index where it is pointing at)
  Symbol *symbol = symbol_lookup(symbol_name);

  // If label by this name does not exist then create a new one
  if (symbol == NULL) {
    symbol = symbol_define(symbol_name, SYM_LABEL);

    if (symbol == NULL) {
      extract_error_message_from_symbol_define_error(&fail_msg, symbol_name);
      goto fail;
    }
  } else if (symbol->kind != SYM_LABEL) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) symbol “%s” already exists and is not a label",
      fn_name,
      symbol_name
    );
    goto fail;
  }

  // Global labels can be updated by executing `(label)` again.
  symbol->as.instruction_index = expression_index;

  // Defining a label preserves the previous evaluation result.
  (void)get_last_evaluation_value(&return_value);
  return return_value;

  fail:
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (goto) — Request evaluation to continue at a global label
Datum std_goto(const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "goto";

  if (!validate_argc(fn_name, &fail_msg, 1, argc)) return return_value;

  if (args[0].kind != SYM_LABEL) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects a label", fn_name);
    goto fail;
  }

  if (args[0].as.instruction_index >= expression_count) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid label destination", fn_name);
    goto fail;
  }

  request_evaluation_jump(args[0].as.instruction_index);

  // `(goto)` by itself does not return anything, but since it’s makes the
  // evaluator jump somewhere else the resulting value will come from there
  // anyway.
  return return_value;

  fail:
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}
