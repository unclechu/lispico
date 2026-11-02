#include <string.h>

#include "expression.h"
#include "wipe-memory.h"
#include "storage.h"

void clean_expressions() {
  // Free parser-allocated strings and other expression data.
  for (uint32_t i = 0; i < expression_allocation_count; ++i) {
    ExpressionAllocation *allocation = &expression_allocations[i];
    if (allocation->data != NULL) {
      wipe_memory(allocation->data, allocation->size);
      free(allocation->data);
    }
  }

  expression_allocation_count = 0;
  expression_count = 0;

  // Wipe host-provided tables without freeing their storage.
  if (expressions != NULL)
    wipe_memory(expressions, (size_t)expression_capacity * sizeof(*expressions));

  if (expression_allocations != NULL)
    wipe_memory(
      expression_allocations,
      (size_t)expression_allocation_capacity * sizeof(*expression_allocations)
    );
}

bool allocate_expression_data(
  const void *source,
  size_t size,
  ValueType element_type,
  uint32_t length,
  uint32_t *allocation_index
) {
  if (allocation_index == NULL) return false;
  if (size != 0 && source == NULL) return false;
  if (
    expression_allocations == NULL ||
    expression_allocation_count >= expression_allocation_capacity
  ) return false;

  void *data = NULL;

  if (size != 0) {
    data = malloc(size);
    if (data == NULL) return false;
    memcpy(data, source, size);
  }

  uint32_t index = expression_allocation_count;

  expression_allocations[index] = (ExpressionAllocation) {
    .data = data,
    .size = size,
    .element_type = element_type,
    .length = length,
  };

  *allocation_index = index;
  expression_allocation_count++;
  return true;
}

const ExpressionAllocation* get_expression_allocation(uint32_t index) {
  if (index >= expression_allocation_count) return NULL;
  return &expression_allocations[index];
}
