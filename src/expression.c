#include <string.h>

#include "expression.h"
#include "wipe-memory.h"

// All parsed expressions.
Expression expressions[EXPR_CAPACITY] = {0};
uint32_t expression_count = 0;

ExpressionAllocation expression_allocations[EXPR_ALLOCATION_CAPACITY] = {0};
uint32_t expression_allocation_count = 0;

void clean_expressions() {
  expression_count = 0;
  wipe_memory(expressions, sizeof(expressions));

  for (uint32_t i = 0; i < expression_allocation_count; ++i) {
    ExpressionAllocation *allocation = &expression_allocations[i];

    if (allocation->data != NULL) {
      wipe_memory(allocation->data, allocation->size);
      free(allocation->data);
    }
  }

  expression_allocation_count = 0;
  wipe_memory(expression_allocations, sizeof(expression_allocations));
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
  if (expression_allocation_count >= EXPR_ALLOCATION_CAPACITY) return false;

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
