#include "stdlib-jumps.h"
#include "stdlib-variables.h"
#include "stdlib-validation-utils.h"
#include "symbol-error.h"
#include "wipe-memory.h"
#include "evaluate.h"
#include "storage.h"

// Define or update a global or local label at the current expression.
static Datum std_label_impl(
  Interpreter *interpreter,
  const Datum *args,
  size_t argc,
  const char *fn_name,
  bool is_local
) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  char symbol_name[SYMBOL_NAME_SIZE] = {'\0'};
  char local_name[SYMBOL_NAME_SIZE] = {'\0'};

  if (!validate_argc(interpreter, fn_name, &fail_msg, 1, argc)) goto finish;

  if (!get_variable_name(&args[0], &symbol_name)) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid label name", fn_name);
    goto fail;
  }

  if (is_local) {
    if (interpreter->local_scope == NULL) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) requires an active (do) scope", fn_name);
      goto fail;
    }

    if (
      !local_symbol_name(interpreter->local_scope->expression_index, symbol_name, &local_name)
    ) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid or too long label name", fn_name);
      goto fail;
    }
  }

  const char *name = is_local ? local_name : symbol_name;
  uint32_t expression_index = get_current_native_call_expression_index(interpreter);

  if (expression_index == EXPR_NONE || expression_index >= interpreter->expression_count) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid expression location", fn_name);
    goto fail;
  }

  // Look up the label in its global or generated local namespace.
  Symbol *symbol = symbol_lookup(interpreter, name);

  // A local label may reuse a reset `TYPE_VOID` entry from an earlier activation.
  if (symbol == NULL) {
    symbol = symbol_define(interpreter, name, SYM_LABEL);

    if (symbol == NULL) {
      extract_error_message_from_symbol_define_error(interpreter, &fail_msg, name);
      goto fail;
    }
  } else if (
    symbol->kind != SYM_LABEL &&
    !(
      is_local &&
      symbol->kind == SYM_VARIABLE &&
      symbol->as.variable.type == TYPE_VOID &&
      symbol->local_scope_owner == NULL
    )
  ) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) symbol “%s” already exists and is not a label",
      fn_name,
      name
    );
    goto fail;
  }

  // A frozen label may be encountered again through (goto), but only at its
  // original expression. Moving the destination would change the symbol.
  if (
    symbol->frozen &&
    (
      symbol->kind != SYM_LABEL ||
      symbol->as.instruction_index != expression_index
    )
  ) {
    (void)validate_symbol_mutable(symbol, fn_name, &fail_msg);
    goto fail;
  }

  // Reuse reset local entries without deleting them from the symbol table.
  // Frozen labels remain valid only at their original expression location.
  if (!symbol->frozen) {
    symbol->kind = SYM_LABEL;
    symbol->as.instruction_index = expression_index;
  }

  if (is_local) register_local_symbol(interpreter, symbol);

  // Defining a label preserves the previous evaluation result.
  (void)get_last_evaluation_value(interpreter, &return_value);
  goto finish;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    goto finish;

  finish:
    wipe_memory(fail_msg, sizeof(fail_msg));
    wipe_memory(symbol_name, sizeof(symbol_name));
    wipe_memory(local_name, sizeof(local_name));
    return return_value;
}

// (label) — Define or update a global label at the current expression.
Datum std_label(Interpreter *interpreter, const Datum *args, size_t argc) {
  return std_label_impl(interpreter, args, argc, "label", false);
}

// (local-label) — Define or update a label in the current (do) scope.
Datum std_local_label(Interpreter *interpreter, const Datum *args, size_t argc) {
  return std_label_impl(interpreter, args, argc, "local-label", true);
}

// (goto) — Request evaluation to continue at a resolved label
Datum std_goto(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "goto";

  if (!validate_argc(interpreter, fn_name, &fail_msg, 1, argc)) return return_value;

  if (args[0].kind != SYM_LABEL) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects a label", fn_name);
    goto fail;
  }

  if (args[0].as.instruction_index >= interpreter->expression_count) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid label destination", fn_name);
    goto fail;
  }

  request_evaluation_jump(interpreter, args[0].as.instruction_index);

  // `(goto)` by itself does not return anything, but since it’s makes the
  // evaluator jump somewhere else the resulting value will come from there
  // anyway.
  return return_value;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}
