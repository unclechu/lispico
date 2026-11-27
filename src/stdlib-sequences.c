#include <inttypes.h>

#include "stdlib-sequences.h"
#include "stdlib-validation-utils.h"
#include "symbol-error.h"
#include "wipe-memory.h"
#include "evaluate.h"

// (last-val) — Read last evaluation result in a sequence of expressions
Datum std_last_val(Interpreter *interpreter, const Datum*, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  char fn_name[] = "last-val";

  if (!validate_argc(interpreter, fn_name, &fail_msg, 0, argc)) return return_value;

  if (!get_last_evaluation_value(interpreter, &return_value)) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) no previous value is available",
      fn_name
    );
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
  }

  return return_value;
}

// (depth) — Get the current `(do)` sequence nesting level
Datum std_depth(Interpreter *interpreter, const Datum*, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "depth";

  if (!validate_argc(interpreter, fn_name, &fail_msg, 0, argc)) return return_value;

  return_value = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = { .type = TYPE_U32, .as.u32 = get_current_sequence_depth(interpreter) },
  };

  return return_value;
}

// (call-depth) — Get the current user-defined function-call nesting depth.
Datum std_call_depth(Interpreter *interpreter, const Datum*, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "call-depth";

  if (!validate_argc(interpreter, fn_name, &fail_msg, 0, argc)) return return_value;

  return_value = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = {
      .type = TYPE_U32,
      .as.u32 = interpreter->function_call == NULL ? 0 : interpreter->function_call->call_depth,
    },
  };

  return return_value;
}

// (fn-depth) — Get the absolute `(depth)` of the current function’s implicit
// `(do)` scope. Unlike `(fn-return-depth)`, this includes the caller’s depth
// and stays fixed when entering explicit `(do)` scopes.
Datum std_fn_depth(Interpreter *interpreter, const Datum*, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "fn-depth";

  if (!validate_argc(interpreter, fn_name, &fail_msg, 0, argc)) return return_value;

  return_value = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = {
      .type = TYPE_U32,
      .as.u32 =
        interpreter->function_call == NULL
          ? 0
          : interpreter->function_call->entry_sequence_depth + 1,
    },
  };

  return return_value;
}

// (fn-return-depth) — Get the number of `(do)` scopes to leave the current
// function from any nesting level, including its implicit function scope.
Datum std_fn_return_depth(Interpreter *interpreter, const Datum*, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "fn-return-depth";

  if (!validate_argc(interpreter, fn_name, &fail_msg, 0, argc)) return return_value;

  uint32_t depth = get_current_function_return_depth(interpreter);

  return_value = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = { .type = TYPE_U32, .as.u32 = depth },
  };

  return return_value;
}

// (get-arg INDEX) — Read a zero-based argument of the active pseudo-function.
Datum std_get_arg(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "get-arg";

  if (!validate_argc(interpreter, fn_name, &fail_msg, 1, argc)) return return_value;

  if (interpreter->function_call == NULL) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) requires an active (fn) call", fn_name);
    goto fail;
  }

  if (args[0].kind != SYM_VARIABLE || args[0].as.value.type != TYPE_U32) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) index must be u32", fn_name);
    goto fail;
  }

  uint32_t index = args[0].as.value.as.u32;
  if (index >= interpreter->function_call->argument_count) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) argument index %" PRIu32 " is out of bounds (argument count is %zu)",
      fn_name,
      index,
      interpreter->function_call->argument_count
    );
    goto fail;
  }

  return interpreter->function_call->arguments[index];

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// Read and validate a requested number of enclosing loops. A loop inside a
// called function is visible only to that invocation, not to its caller.
static uint32_t validate_loop_depth(
  Interpreter *interpreter,
  const Datum *args,
  size_t argc,
  const char *fn_name
) {
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  if (argc > 1) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects 0 or 1 argument", fn_name);
    goto fail;
  }

  uint32_t count = 1;
  if (argc == 1) {
    if (args[0].kind != SYM_VARIABLE || args[0].as.value.type != TYPE_U32) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) count must be u32", fn_name);
      goto fail;
    }
    count = args[0].as.value.as.u32;
  }

  if (count == 0) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) count must be greater than 0", fn_name);
    goto fail;
  }

  uint32_t depth = get_loop_break_depth(interpreter, count);
  if (depth == 0) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) count exceeds enclosing loops", fn_name);
    goto fail;
  }
  return depth;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return 0;
}

// (loop-break-depth [N]) — Number of sequence scopes needed to exit N loops.
Datum std_loop_break_depth(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum result = { .kind = SYM_EMPTY };
  uint32_t depth = validate_loop_depth(interpreter, args, argc, "loop-break-depth");
  if (depth == 0) return result;

  result = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = { .type = TYPE_U32, .as.u32 = depth },
  };
  return result;
}

// (break [N]) — Leave N enclosing loops, including their implicit scopes.
Datum std_break(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum result = { .kind = SYM_EMPTY };
  uint32_t depth = validate_loop_depth(interpreter, args, argc, "break");
  if (depth == 0) return result;

  // The evaluator unwinds through `(do)` and loop scopes, running defers.
  // As with `(leave)`, the most recent evaluation result is forwarded.
  (void)get_last_evaluation_value(interpreter, &result);
  interpreter->pending_leave_count = depth;
  return result;
}
