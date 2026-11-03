#include <string.h>

#include "expression.h"
#include "wipe-memory.h"
#include "storage.h"

void clean_expressions(Interpreter *interpreter) {
  if (interpreter == NULL || !interpreter->configured) return;

  Expression *expressions = interpreter->storage.expressions;
  ExpressionAllocation *allocations = interpreter->storage.expression_allocations;

  if (allocations != NULL)
    for (uint32_t i = 0; i < interpreter->expression_allocation_count; ++i) {
      ExpressionAllocation *allocation = &allocations[i];

      if (allocation->data != NULL) {
        wipe_memory(allocation->data, allocation->size);
        free(allocation->data);
      }
    }

  interpreter->expression_allocation_count = 0;
  interpreter->expression_count = 0;

  if (expressions != NULL)
    wipe_memory(
      expressions,
      (size_t)interpreter->storage.expression_capacity *
      sizeof(*expressions)
    );

  if (allocations != NULL)
    wipe_memory(
      allocations,
      (size_t)interpreter->storage.expression_allocation_capacity *
      sizeof(*allocations)
    );
}

bool allocate_expression_data(
  Interpreter *interpreter,
  const void *source,
  size_t size,
  ValueType element_type,
  uint32_t length,
  uint32_t *allocation_index
) {
  ExpressionAllocation *expression_allocations = interpreter->storage.expression_allocations;

  if (
    interpreter->expression_allocation_count >=
    interpreter->storage.expression_allocation_capacity
  ) return false;

  if (allocation_index == NULL) return false;
  if (size != 0 && source == NULL) return false;
  if (
    expression_allocations == NULL ||
    interpreter->expression_allocation_count >= interpreter->storage.expression_allocation_capacity
  ) return false;

  void *data = NULL;

  if (size != 0) {
    data = malloc(size);
    if (data == NULL) return false;
    memcpy(data, source, size);
  }

  uint32_t index = interpreter->expression_allocation_count;

  expression_allocations[index] = (ExpressionAllocation) {
    .data = data,
    .size = size,
    .element_type = element_type,
    .length = length,
  };

  *allocation_index = index;
  interpreter->expression_allocation_count++;
  return true;
}

const ExpressionAllocation* get_expression_allocation(
  const Interpreter *interpreter,
  uint32_t index
) {
  if (interpreter == NULL || !interpreter->configured) return NULL;
  if (index >= interpreter->expression_allocation_count) return NULL;
  return &interpreter->storage.expression_allocations[index];
}
