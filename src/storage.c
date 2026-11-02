#include "storage.h"
#include "wipe-memory.h"

// *** Active host-provided storage and its usage ***

// Expressions
Expression *expressions = NULL;
uint32_t expression_capacity = 0;
uint32_t expression_count = 0;

// Allocated expression entities (strings & arrays)
ExpressionAllocation *expression_allocations = NULL;
uint32_t expression_allocation_capacity = 0;
uint32_t expression_allocation_count = 0;

// Symbols
Symbol *symbols = NULL;
uint32_t symbol_capacity = 0;
uint32_t symbol_count = 0;

// Runtime allocation records
EvalRuntimeAllocation *runtime_allocations = NULL;
uint32_t runtime_allocation_capacity = 0;
uint32_t runtime_allocation_count = 0;

// Deferred expression indices
uint32_t *deferred_expression_indices = NULL;
uint32_t deferred_expression_capacity = 0;
uint32_t deferred_expression_count = 0;

// *** END: Active host-provided storage and its usage ***

// Bind host-provided storage without taking ownership of its memory.
bool configure_interpreter_storage(const InterpreterStorage *storage) {
  static bool configured = false;

  if (configured || storage == NULL) return false;

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

  // Open-addressed symbol hashing requires a power-of-two capacity.
  if ((storage->symbol_capacity & (storage->symbol_capacity - 1u)) != 0) return false;

  #if SIZE_MAX <= UINT32_MAX
    if (
      storage->expression_capacity > SIZE_MAX / sizeof(*storage->expressions) ||
      storage->expression_allocation_capacity > SIZE_MAX / sizeof(*storage->expression_allocations) ||
      storage->symbol_capacity > SIZE_MAX / sizeof(*storage->symbols) ||
      storage->runtime_allocation_capacity > SIZE_MAX / sizeof(*storage->runtime_allocations) ||
      storage->deferred_expression_capacity > SIZE_MAX / sizeof(*storage->deferred_expression_indices)
    ) return false;
  #endif

  expressions = storage->expressions;
  expression_capacity = storage->expression_capacity;
  expression_count = 0;

  expression_allocations = storage->expression_allocations;
  expression_allocation_capacity = storage->expression_allocation_capacity;
  expression_allocation_count = 0;

  symbols = storage->symbols;
  symbol_capacity = storage->symbol_capacity;
  symbol_count = 0;

  runtime_allocations = storage->runtime_allocations;
  runtime_allocation_capacity = storage->runtime_allocation_capacity;
  runtime_allocation_count = 0;

  deferred_expression_indices = storage->deferred_expression_indices;
  deferred_expression_capacity = storage->deferred_expression_capacity;
  deferred_expression_count = 0;

  // Start with empty tables, including when the host supplies stack memory.
  wipe_memory(expressions, (size_t)expression_capacity * sizeof(*expressions));
  wipe_memory(
    expression_allocations,
    (size_t)expression_allocation_capacity * sizeof(*expression_allocations)
  );
  wipe_memory(symbols, (size_t)symbol_capacity * sizeof(*symbols));
  wipe_memory(
    runtime_allocations,
    (size_t)runtime_allocation_capacity * sizeof(*runtime_allocations)
  );
  wipe_memory(
    deferred_expression_indices,
    (size_t)deferred_expression_capacity * sizeof(*deferred_expression_indices)
  );

  configured = true;
  return true;
}
