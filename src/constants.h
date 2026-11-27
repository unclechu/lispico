#pragma once

// Maximum error message size
#define ERR_MSG_SIZE 2048u

// *** Symbols ***

// Must be a power of two (for bitwise-modulo optimizations to work — &)
#define SYMBOL_CAPACITY 512u
// Maximum Symbol name length (including \0 terminator).
#define SYMBOL_NAME_SIZE 64u

static_assert(
  SYMBOL_CAPACITY > 0 &&
  (SYMBOL_CAPACITY & (SYMBOL_CAPACITY - 1u)) == 0,
  "SYMBOL_CAPACITY must be a power of two"
);

// *** Expressions ***

// Maximum expression storage capacity
#define EXPR_CAPACITY 2048u

// Maximum amount of allocated expression entries (e.g. strings or arrays)
#define EXPR_ALLOCATION_CAPACITY 512u

// Maximum limit for a string literal in an expression
#define EXPR_TEXT_SIZE 256u

// *** Evaluation ***

// Maximum arguments limit for an expression to have
#define EVAL_ARG_CAPACITY 32u

// Maximum number of nested user-defined function calls
#define EVAL_FUNCTION_CALL_DEPTH_LIMIT 128u

// Maximum amount of dynamically allocated runtime entities
#define EVAL_RUNTIME_ALLOCATION_CAPACITY 512u

// Maximum number of simultaneously registered deferred expressions
#define EVAL_DEFER_CAPACITY 256u

// Number of cached structural `(goto)` routes (two entries per bucket).
// Set to zero to disable caching; otherwise use a power of two >= 2.
#ifndef EVAL_JUMP_CACHE_ENTRY_COUNT
  #define EVAL_JUMP_CACHE_ENTRY_COUNT 32u
#endif

static_assert(
  EVAL_JUMP_CACHE_ENTRY_COUNT == 0 ||
  (
    EVAL_JUMP_CACHE_ENTRY_COUNT >= 2 &&
    (EVAL_JUMP_CACHE_ENTRY_COUNT & (EVAL_JUMP_CACHE_ENTRY_COUNT - 1u)) == 0
  ),
  "EVAL_JUMP_CACHE_ENTRY_COUNT must be zero or a power of two >= 2"
);

// User-defined functions are registered as regular labels using this template
#define EVAL_FN_LABEL_FORMAT_TEMPLATE "_FN_%s"

// *** Parsing ***

// Maximum depth of nested S-expression lists
#define PARSER_MAX_DEPTH 64u

// Maximum buffer size (including `\0` terminator)
// for parsing and formatting numeric text.
#define NUMBER_TEXT_CAPACITY 128u

// *** Source registry ***

// Initial (include) files registry dynamic capacity (when dynamic growth is enabled).
#define SOURCE_REGISTRY_DYNAMIC_INITIAL_CAPACITY 8u

// The capacity reallocation/growth formula for dynamic source registry.
#define SOURCE_REGISTRY_DYNAMIC_CAPACITY_GROW(capacity) ((capacity) * 2u)

// *** Reading contents ***

// 64KiB
#define SOURCE_CAPACITY 65536u
