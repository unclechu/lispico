#pragma once

// Must be a power of two (for bitwise-modulo optimizations to work — &)
#define SYMBOL_CAPACITY 256u
// Maximum Symbol name length (including \0 terminator).
#define SYMBOL_NAME_SIZE 128u

// Maximum error message size
#define ERR_MSG_SIZE 2048u

// Maximum expression storage capacity
#define EXPR_CAPACITY 2048u

// Maximum limit for a string literal in an expression
#define EXPR_TEXT_SIZE 256u

// Maximum arguments limit for an expression
#define EVAL_ARG_CAPACITY 16u

static_assert(
  SYMBOL_CAPACITY > 0 &&
  (SYMBOL_CAPACITY & (SYMBOL_CAPACITY - 1u)) == 0,
  "SYMBOL_CAPACITY must be a power of two"
);
