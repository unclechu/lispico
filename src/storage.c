#include "storage.h"
#include "wipe-memory.h"

bool configure_interpreter_storage(Interpreter *interpreter, const InterpreterStorage *storage) {
  if (interpreter == NULL || storage == NULL || interpreter->configured) return false;

  if (
    storage->expressions == NULL ||
    storage->expression_capacity == 0 ||
    storage->expression_allocations == NULL ||
    storage->expression_allocation_capacity == 0 ||
    storage->symbols == NULL ||
    storage->symbol_capacity == 0 ||
    storage->runtime_allocations == NULL ||
    storage->runtime_allocation_capacity == 0 ||
    storage->deferred_expression_indices == NULL ||
    storage->deferred_expression_capacity == 0
  ) return false;

  // Source locations are optional, but the table must cover every expression.
  if (
    (
      storage->expression_source_locations == NULL &&
      storage->expression_source_location_capacity != 0
    ) || (
      storage->expression_source_locations != NULL &&
      storage->expression_source_location_capacity < storage->expression_capacity
    )
  ) return false;

  // Open-addressed symbol hashing requires a power-of-two capacity.
  if ((storage->symbol_capacity & (storage->symbol_capacity - 1u)) != 0) return false;

  #if SIZE_MAX <= UINT32_MAX
    if (
      storage->expression_capacity
        > (SIZE_MAX / sizeof(*storage->expressions)) ||

      (
        storage->expression_source_locations != NULL &&
        storage->expression_source_location_capacity
          > (SIZE_MAX / sizeof(*storage->expression_source_locations))
      ) ||

      storage->expression_allocation_capacity
        > (SIZE_MAX / sizeof(*storage->expression_allocations)) ||

      storage->symbol_capacity
        > (SIZE_MAX / sizeof(*storage->symbols)) ||

      storage->runtime_allocation_capacity
        > (SIZE_MAX / sizeof(*storage->runtime_allocations)) ||

      storage->deferred_expression_capacity
        > (SIZE_MAX / sizeof(*storage->deferred_expression_indices))
    ) return false;
  #endif

  *interpreter = (Interpreter) {
    .storage = *storage,
    .current_native_call_expression_index = EXPR_NONE,
    .pending_jump_index = EXPR_NONE,
    .resume_target_index = EXPR_NONE,
    .eval_outcome = { .kind = EVAL_OK, .result = { .kind = SYM_EMPTY } },
    .configured = true,
  };

  // Initialize host-provided tables without taking ownership.
  wipe_memory(
    storage->expressions,
    (size_t)storage->expression_capacity * sizeof(*storage->expressions)
  );
  if (storage->expression_source_locations != NULL)
    wipe_memory(
      storage->expression_source_locations,
      (size_t)storage->expression_source_location_capacity
        * sizeof(*storage->expression_source_locations)
    );
  wipe_memory(
    storage->expression_allocations,
    (size_t)storage->expression_allocation_capacity * sizeof(*storage->expression_allocations)
  );
  wipe_memory(
    storage->symbols,
    (size_t)storage->symbol_capacity * sizeof(*storage->symbols)
  );
  wipe_memory(
    storage->runtime_allocations,
    (size_t)storage->runtime_allocation_capacity * sizeof(*storage->runtime_allocations)
  );
  wipe_memory(
    storage->deferred_expression_indices,
    (size_t)storage->deferred_expression_capacity * sizeof(*storage->deferred_expression_indices)
  );

  return true;
}
