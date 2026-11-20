#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../src/storage.h"
#include "../../src/stdlib-variables.h"
#include "../../src/stdlib-gc.h"
#include "../../src/evaluate.h"

static bool has_name(const Datum *names, const char *name) {
  assert(names->kind == SYM_ARRAY);
  assert(names->as.array.element_type == TYPE_STRING);
  const String *strings = names->as.array.data;

  for (uint32_t i = 0; i < names->as.array.length; ++i) {
    if (strcmp(strings[i].data, name) == 0) return true;
  }
  return false;
}

int main(void) {
  static Expression expressions[16] = {0};
  static ExpressionAllocation expression_allocations[16] = {0};
  static Symbol symbols[64] = {0};
  static EvalRuntimeAllocation runtime_allocations[512] = {0};
  static uint32_t deferred_indices[16] = {0};

  InterpreterStorage storage = {
    .expressions = expressions,
    .expression_capacity = 16,
    .expression_allocations = expression_allocations,
    .expression_allocation_capacity = 16,
    .symbols = symbols,
    .symbol_capacity = 64,
    .runtime_allocations = runtime_allocations,
    .runtime_allocation_capacity = 512,
    .deferred_expression_indices = deferred_indices,
    .deferred_expression_capacity = 16,
  };
  Interpreter interpreter = {0};
  assert(configure_interpreter_storage(&interpreter, &storage));

  // Both functions return typed empty arrays when no symbols exist.
  Datum empty = std_variables(&interpreter, NULL, 0);
  assert(empty.kind == SYM_ARRAY);
  assert(empty.as.array.element_type == TYPE_STRING);
  assert(empty.as.array.length == 0);
  empty = std_local_variables(&interpreter, NULL, 0);
  assert(empty.as.array.length == 0);

  Symbol *global = symbol_define(&interpreter, "value", SYM_VARIABLE);
  assert(global != NULL);
  global->as.variable = (Value) { .type = TYPE_U32, .as.u32 = 10 };

  // This name resembles a generated local but was defined globally.
  Symbol *unrelated = symbol_define(&interpreter, "_LOCAL_999_manual", SYM_VARIABLE);
  assert(unrelated != NULL);
  unrelated->as.variable = (Value) { .type = TYPE_VOID };

  Symbol *label = symbol_define(&interpreter, "jump", SYM_LABEL);
  assert(label != NULL);
  Symbol *native = symbol_define(&interpreter, "native", SYM_NATIVE_FUNCTION);
  assert(native != NULL);

  // Raw enumeration includes every occupied slot, not only variable kinds.
  Datum globals = std_variables(&interpreter, NULL, 0);
  assert(globals.as.array.length == 4);
  assert(has_name(&globals, "value"));
  assert(has_name(&globals, "_LOCAL_999_manual"));
  assert(has_name(&globals, "jump"));
  assert(has_name(&globals, "native"));
  const String *sorted_names = globals.as.array.data;
  for (uint32_t i = 1; i < globals.as.array.length; ++i)
    assert(strcmp(sorted_names[i - 1].data, sorted_names[i].data) < 0);

  EvalLocalScopeFrame outer = { .expression_index = 12 };
  interpreter.local_scope = &outer;
  interpreter.active_local_scope = &outer;

  Symbol *local = symbol_define(&interpreter, "_LOCAL_12_value", SYM_VARIABLE);
  assert(local != NULL);
  local->as.variable = (Value) { .type = TYPE_U32, .as.u32 = 20 };
  register_local_symbol(&interpreter, local);
  assert(local->defined_as_local);

  Symbol *only_local = symbol_define(&interpreter, "_LOCAL_12_only", SYM_ARRAY);
  assert(only_local != NULL);
  only_local->as.array = (Array) { .element_type = TYPE_U32, .length = 0 };
  register_local_symbol(&interpreter, only_local);

  // A local label masks a global variable, and remains visible as a label.
  Symbol *masked_global = symbol_define(&interpreter, "masked", SYM_VARIABLE);
  assert(masked_global != NULL);
  masked_global->as.variable = (Value) { .type = TYPE_U32, .as.u32 = 100 };
  Symbol *masking_label = symbol_define(&interpreter, "_LOCAL_12_masked", SYM_LABEL);
  assert(masking_label != NULL);
  register_local_symbol(&interpreter, masking_label);

  Datum visible = std_local_variables(&interpreter, NULL, 0);
  assert(visible.as.array.length == 6);
  assert(!has_name(&visible, "value"));
  assert(has_name(&visible, "_LOCAL_12_value"));
  assert(has_name(&visible, "_LOCAL_12_only"));
  assert(!has_name(&visible, "masked"));
  assert(has_name(&visible, "_LOCAL_12_masked"));
  assert(has_name(&visible, "jump"));
  assert(has_name(&visible, "native"));
  assert(has_name(&visible, "_LOCAL_999_manual"));

  // Raw enumeration keeps both sides of every shadowing relationship.
  globals = std_variables(&interpreter, NULL, 0);
  assert(globals.as.array.length == 8);
  assert(has_name(&globals, "value"));
  assert(has_name(&globals, "_LOCAL_12_value"));
  assert(has_name(&globals, "masked"));
  assert(has_name(&globals, "_LOCAL_12_masked"));

  EvalLocalScopeFrame inner = { .expression_index = 13, .previous = &outer };
  interpreter.local_scope = &inner;
  interpreter.active_local_scope = &inner;
  Symbol *inner_local = symbol_define(&interpreter, "_LOCAL_13_value", SYM_VARIABLE);
  assert(inner_local != NULL);
  inner_local->as.variable = (Value) { .type = TYPE_U32, .as.u32 = 30 };
  register_local_symbol(&interpreter, inner_local);

  visible = std_local_variables(&interpreter, NULL, 0);
  assert(visible.as.array.length == 6);
  assert(has_name(&visible, "_LOCAL_13_value"));
  assert(!has_name(&visible, "_LOCAL_12_value"));
  assert(!has_name(&visible, "value"));
  assert(has_name(&visible, "_LOCAL_12_only"));

  globals = std_variables(&interpreter, NULL, 0);
  assert(globals.as.array.length == 9);
  assert(has_name(&globals, "_LOCAL_13_value"));
  assert(has_name(&globals, "_LOCAL_12_value"));

  // Suspended caller scopes do not participate in definition-site lookup.
  EvalLocalScopeFrame function_scope = {
    .expression_index = 14,
    .previous_active = &inner,
  };
  interpreter.local_scope = &function_scope;
  interpreter.active_local_scope = &function_scope;
  visible = std_local_variables(&interpreter, NULL, 0);
  assert(visible.as.array.length == 5);
  assert(has_name(&visible, "value"));
  assert(has_name(&visible, "masked"));
  assert(!has_name(&visible, "_LOCAL_13_value"));
  assert(!has_name(&visible, "_LOCAL_12_only"));

  // Inactive local entries remain in the raw table but not scoped lookup.
  interpreter.local_scope = NULL;
  interpreter.active_local_scope = NULL;
  visible = std_local_variables(&interpreter, NULL, 0);
  assert(visible.as.array.length == 5);
  assert(has_name(&visible, "value"));
  assert(has_name(&visible, "masked"));
  assert(!has_name(&visible, "_LOCAL_12_only"));

  globals = std_variables(&interpreter, NULL, 0);
  assert(globals.as.array.length == 9);
  assert(has_name(&globals, "_LOCAL_12_only"));
  assert(has_name(&globals, "_LOCAL_13_value"));

  // Returned names remain independent after the original table is cleaned.
  assert(has_name(&globals, "_LOCAL_13_value"));
  clean_symbols(&interpreter);
  assert(has_name(&globals, "_LOCAL_13_value"));

  // An active evaluator GC root keeps the result and its names alive.
  EvalGCRootFrame root = { .values = &globals, .count = 1 };
  interpreter.gc_roots = &root;
  Datum reclaimed = std_gc(&interpreter, NULL, 0);
  assert(reclaimed.kind == SYM_VARIABLE);
  assert(interpreter.eval_outcome.kind == EVAL_OK);
  assert(has_name(&globals, "_LOCAL_13_value"));
  interpreter.gc_roots = NULL;

  interpreter.eval_outcome.kind = EVAL_OK;
  Datum bad_arg = { .kind = SYM_VARIABLE, .as.value = { .type = TYPE_U32 } };
  Datum failure = std_local_variables(&interpreter, &bad_arg, 1);
  assert(failure.kind == SYM_EMPTY);
  assert(interpreter.eval_outcome.kind == EVAL_FAIL);

  clean_evaluation(&interpreter);
  clean_symbols(&interpreter);
  clean_expressions(&interpreter);

  puts("Variable enumeration tests passed");
  return 0;
}
