#pragma once

#include <stdint.h>
#include <stddef.h>

#include "constants.h"
#include "symbol.h"

// Using upper boundary as an equivalent of expression termination
// (easy to serialize into a binary)
#define EXPR_NONE UINT32_MAX

typedef enum {
  EXPR_LIST = 1,
  EXPR_NAME, // Name literal (e.g. variable name or function name such as `+` or `last-val`)
  EXPR_U32,
  EXPR_I32,
  EXPR_F32,
  EXPR_STRING, // String literal (e.g. `"foo"`)
  EXPR_ARRAY, // Array literal (e.g. `[123, 321]`)
} ExpressionKind;

static_assert(
  sizeof(int32_t) <= sizeof(uint32_t) &&
  sizeof(float) <= sizeof(uint32_t),
  "Scalars must not exceed uint32_t size in order to fit their bits into serialized uint32_t"
);

// `uint32_t` is selected for expression array to be serializable into a binary
// representation (for pre-compiled-ish binaries).
typedef struct {
  uint32_t kind;

  // Index pointer to the first child (e.g. first argument of a function call)
  uint32_t first_child;

  // Index pointer to next sibling expression
  uint32_t next;

  // Contains either scalar bits (`u32`, `i32`, `f32`)
  // or an index pointing to an entry of `expression_allocations` array
  // (for `kind == EXPR_STRING` or `kind == EXPR_ARRAY`).
  uint32_t data;
} Expression;

// Optional location of an expression in its original source.
// The array index matches the corresponding `Expression` index.
typedef struct {
  uint32_t byte_offset; // Zero-based byte position in the source
  uint32_t line; // One-based source line
  uint32_t column; // One-based byte column
} ExpressionSourceLocation;

typedef struct {
  void *data; // A pointer to the allocated memory (must be `free`-d when resetting evaluation)
  size_t size; // Allocated size in bytes (corresponds to `sizeof` value)

  ValueType element_type; // For array allocations
  uint32_t length; // String length or array element count
} ExpressionAllocation;

bool allocate_expression_data(
  Interpreter *interpreter,
  const void *source,
  size_t size,
  ValueType element_type,
  uint32_t length,
  uint32_t *allocation_index
);

const ExpressionAllocation* get_expression_allocation(
  const Interpreter *interpreter,
  uint32_t index
);

// Returns NULL when source mapping is disabled or the index has no location.
const ExpressionSourceLocation* get_expression_source_location(
  const Interpreter *interpreter,
  uint32_t index
);

void clean_expressions(Interpreter *interpreter);
