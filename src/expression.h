#pragma once

#include <stdint.h>

#include "constants.h"

// Using upper boundary as an equivalent of expression termination
// (easy to serialize into a binary)
#define EXPR_NONE UINT32_MAX

typedef enum {
  EXPR_LIST = 1,
  EXPR_NAME,
  EXPR_U32,
  EXPR_I32,
  EXPR_F32,
  EXPR_STRING,
} ExpressionKind;

// `uint32_t` is selected for expression array to be serializable into a binary
// representation (for pre-compiled-ish binaries).
typedef struct {
  uint32_t kind;
  uint32_t first_child;
  uint32_t next;
  // Refactor this using an allocation pool
  // (one expression occupies lots of memory,
  // this won’t go well with Pico tiny RAM for example).
  union {
    uint32_t u32;
    int32_t i32;
    float f32;
    char text[EXPR_TEXT_SIZE];
  } as;
} Expression;

void clean_expressions();

// Global fixed-memory expressions storage
extern Expression expressions[EXPR_CAPACITY];
extern uint32_t expression_count;
