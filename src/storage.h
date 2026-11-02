#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "expression.h"
#include "symbol.h"

// Record of an allocation owned by the evaluator.
typedef struct {
  void *data;
  size_t size;
} EvalRuntimeAllocation;

// Storage supplied and owned by the host application.
typedef struct {
  Expression *expressions;
  uint32_t expression_capacity;

  ExpressionAllocation *expression_allocations;
  uint32_t expression_allocation_capacity;

  Symbol *symbols;
  uint32_t symbol_capacity;

  EvalRuntimeAllocation *runtime_allocations;
  uint32_t runtime_allocation_capacity;

  uint32_t *deferred_expression_indices;
  uint32_t deferred_expression_capacity;
} InterpreterStorage;

// Configure storage before parsing or evaluating.
// Returns `false` for invalid storage or repeated initialization.
bool configure_interpreter_storage(const InterpreterStorage *storage);

// *** Active host-provided storage and its usage ***
//
// TODO: Subject to refactoring.
//       Make this state local instead of global.
//       This would allow multiple independent interpreter instances within one process.

extern Expression *expressions;
extern uint32_t expression_capacity;
extern uint32_t expression_count;

extern ExpressionAllocation *expression_allocations;
extern uint32_t expression_allocation_capacity;
extern uint32_t expression_allocation_count;

extern Symbol *symbols;
extern uint32_t symbol_capacity;
extern uint32_t symbol_count;

extern EvalRuntimeAllocation *runtime_allocations;
extern uint32_t runtime_allocation_capacity;
extern uint32_t runtime_allocation_count;

extern uint32_t *deferred_expression_indices;
extern uint32_t deferred_expression_capacity;
extern uint32_t deferred_expression_count;
