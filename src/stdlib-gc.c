#include <stdlib.h>

#include "stdlib-gc.h"
#include "stdlib-validation-utils.h"
#include "evaluate.h"
#include "storage.h"
#include "wipe-memory.h"

// Mark a runtime allocation if its data pointer is referenced by a live value.
// Parser allocations, static strings, and host-owned data are not runtime-owned.
static void mark_runtime_data(Interpreter *interpreter, const void *data) {
  if (data == NULL) return;

  for (uint32_t i = 0; i < interpreter->runtime_allocation_count; ++i) {
    EvalRuntimeAllocation *allocation = &interpreter->storage.runtime_allocations[i];
    if (allocation->data == data) {
      allocation->marked = true;
      return;
    }
  }
}

// Mark the runtime allocations reachable through one scalar or array value.
// Arrays are one-dimensional; only arrays of strings contain further pointers.
static void mark_datum_runtime_data(Interpreter *interpreter, const Datum *datum) {
  if (datum->kind == SYM_VARIABLE) {
    if (datum->as.value.type == TYPE_STRING)
      mark_runtime_data(interpreter, datum->as.value.as.string.data);
  } else if (datum->kind == SYM_ARRAY) {
    Array array = datum->as.array;
    mark_runtime_data(interpreter, array.data);

    if (array.element_type == TYPE_STRING && array.data != NULL) {
      const String *strings = array.data;
      for (uint32_t i = 0; i < array.length; ++i)
        mark_runtime_data(interpreter, strings[i].data);
    }
  }
}

// Mark every tracked allocation reachable from symbols or active evaluator values.
// The hash table's entries are spread across `symbol_capacity`, not `symbol_count`.
static void mark_reachable_runtime_data(Interpreter *interpreter) {
  for (uint32_t i = 0; i < interpreter->storage.symbol_capacity; ++i) {
    const Symbol *symbol = &interpreter->storage.symbols[i];
    Datum value = { .kind = SYM_EMPTY };

    if (symbol->kind == SYM_VARIABLE) {
      value = (Datum) { .kind = SYM_VARIABLE, .as.value = symbol->as.variable };
    } else if (symbol->kind == SYM_ARRAY) {
      value = (Datum) { .kind = SYM_ARRAY, .as.array = symbol->as.array };
    } else {
      continue;
    }

    mark_datum_runtime_data(interpreter, &value);
  }

  // Keep the most recently evaluated value alive, even if it has no symbol.
  mark_datum_runtime_data(interpreter, &interpreter->eval_outcome.result);

  // Traverse all active evaluator root frames. Each frame references either
  // a single `Datum` (`count == 1`) or an array of consecutive `Datum` values.
  // Mark every value in each frame so temporary allocations remain reachable.
  for (
    const EvalGCRootFrame *frame = interpreter->gc_roots;
    frame != NULL;
    frame = frame->previous
  )
    for (size_t i = 0; i < frame->count; ++i)
      mark_datum_runtime_data(interpreter, &frame->values[i]);
}

// (gc) — Free unreachable runtime allocations and return how many were freed.
Datum std_gc(Interpreter *interpreter, const Datum*, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "gc";

  if (!validate_argc(interpreter, fn_name, &fail_msg, 0, argc)) return return_value;

  // No heap allocation is needed to mark reachable entities: a mark bit is
  // stored in each `EvalRuntimeAllocation` and all existing marks are clear.
  mark_reachable_runtime_data(interpreter);

  uint32_t freed = 0;
  uint32_t i = 0;

  while (i < interpreter->runtime_allocation_count) {
    EvalRuntimeAllocation *allocation = &interpreter->storage.runtime_allocations[i];

    if (allocation->marked) {
      // Clear surviving marks now, preparing them for the next (gc) call.
      allocation->marked = false;
      ++i;
      continue;
    }

    // Unreachable allocation: wipe its entire tracked capacity before freeing.
    wipe_memory(allocation->data, allocation->size);
    free(allocation->data);

    // Move the last entry into the released slot, keeping the table packed.
    uint32_t last_index = --interpreter->runtime_allocation_count;
    if (i != last_index)
      interpreter->storage.runtime_allocations[i] =
        interpreter->storage.runtime_allocations[last_index];

    // The old last slot is now outside the active allocation range.
    interpreter->storage.runtime_allocations[last_index] = (EvalRuntimeAllocation) {0};
    ++freed;

    // Do not advance `i`: the moved allocation must still be checked.
  }

  return_value = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = { .type = TYPE_U32, .as.u32 = freed },
  };

  return return_value;
}
