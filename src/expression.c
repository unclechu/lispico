#include "expression.h"
#include "wipe-memory.h"

// All parsed expressions.
Expression expressions[EXPR_CAPACITY] = {0};
uint32_t expression_count = 0;

void clean_expressions() {
  expression_count = 0;
  wipe_memory(expressions, sizeof(expressions));
}
