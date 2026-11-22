#include <string.h>

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
    EVAL_FN_LABEL_FORMAT_TEMPLATE,
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

// (apply FUNCTION-NAME [ARG…] ARRAY) — Append the final array’s elements
// as individual arguments and dispatch through `(call)`.
Datum std_apply(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "apply";

  if (argc < 2) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects at least 2 arguments", fn_name);
    goto fail;
  }

  const Datum *tail_arg = &args[argc - 1];
  if (tail_arg->kind != SYM_ARRAY) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects an array as the last argument", fn_name);
    goto fail;
  }

  const Array *tail = &tail_arg->as.array;
  if (tail->length != 0 && tail->data == NULL) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid array data", fn_name);
    goto fail;
  }

  // The expanded call has the same argument limit as an ordinary `(call)`,
  // including the function name. No dynamic argument storage is needed.
  size_t fixed_argc = argc - 1;
  if (fixed_argc > EVAL_ARG_CAPACITY || tail->length > EVAL_ARG_CAPACITY - fixed_argc) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) too many expanded arguments (maximum is %u)",
      fn_name,
      EVAL_ARG_CAPACITY
    );
    goto fail;
  }

  switch (tail->element_type) {
    case TYPE_U32:
    case TYPE_I32:
    case TYPE_F32:
    case TYPE_STRING:
      break;
    default:
      goto unsupported_array_element_type_fail;
  }

  Datum expanded_args[EVAL_ARG_CAPACITY] = {0};
  memcpy(expanded_args, args, fixed_argc * sizeof(Datum));

  // Preserve fixed arguments and append the array elements in their order.
  // String elements retain their original length and data references.
  for (size_t i = 0; i < tail->length; ++i) {
    Value *value = &expanded_args[fixed_argc + i].as.value;
    expanded_args[fixed_argc + i].kind = SYM_VARIABLE;
    value->type = tail->element_type;

    switch (tail->element_type) {
      case TYPE_U32: value->as.u32 = ((const uint32_t *)tail->data)[i]; break;
      case TYPE_I32: value->as.i32 = ((const int32_t *)tail->data)[i]; break;
      case TYPE_F32: value->as.f32 = ((const float *)tail->data)[i]; break;
      case TYPE_STRING: {
        value->as.string = ((const String *)tail->data)[i];
        if (value->as.string.length != 0 && value->as.string.data == NULL) {
          snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid string array element", fn_name);
          wipe_memory(expanded_args, sizeof(expanded_args));
          goto fail;
        }
        break;
      }
      default:
        // Validated above (report just in case)
        goto unsupported_array_element_type_fail;
    }
  }

  // Keep expanded string values alive even when the called function runs `(gc)`.
  EvalGCRootFrame roots = {
    .values = expanded_args,
    .count = fixed_argc + tail->length,
    .previous = interpreter->gc_roots,
  };
  interpreter->gc_roots = &roots;

  return_value = std_call(interpreter, expanded_args, roots.count);

  interpreter->gc_roots = roots.previous;
  wipe_memory(&roots, sizeof(roots));
  wipe_memory(expanded_args, sizeof(expanded_args));
  return return_value;

  unsupported_array_element_type_fail:
    snprintf(fail_msg, sizeof(fail_msg), "(%s) unsupported array element type", fn_name);
    goto fail;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}
