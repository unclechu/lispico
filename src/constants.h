#pragma once

// Maximum error message size
#define ERR_MSG_SIZE 2048u

// *** Symbols ***

// Must be a power of two (for bitwise-modulo optimizations to work — &)
#define SYMBOL_CAPACITY 256u
// Maximum Symbol name length (including \0 terminator).
#define SYMBOL_NAME_SIZE 32u

static_assert(
  SYMBOL_CAPACITY > 0 &&
  (SYMBOL_CAPACITY & (SYMBOL_CAPACITY - 1u)) == 0,
  "SYMBOL_CAPACITY must be a power of two"
);

// *** Expressions ***

// Maximum expression storage capacity
#define EXPR_CAPACITY 2048u

// Maximum amount of allocated expression entries (e.g. strings or arrays)
#define EXPR_ALLOCATION_CAPACITY 256u

// Maximum limit for a string literal in an expression
#define EXPR_TEXT_SIZE 256u

// *** Evaluation ***

// Maximum arguments limit for an expression to have
#define EVAL_ARG_CAPACITY 16u

#define EVAL_RUNTIME_ALLOCATION_CAPACITY 256u

// *** Parsing ***

// Maximum depth of nested S-expression lists
#define PARSER_MAX_DEPTH 64u

// Maximum buffer size (including `\0` terminator)
// for a string containing a number to be parsed
#define NUMBER_TEXT_CAPACITY 128u

// *** Reading contents ***

// 64KiB
#define SOURCE_CAPACITY 65536u
