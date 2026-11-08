#pragma once

#include "constants.h"
#include "storage.h"

bool populate_stdlib(Interpreter *interpreter, char (*error_message)[ERR_MSG_SIZE]);

// For this to work you need to include all the associated `stdlib-*.h` header
// files that provide these macros.
#define POPULATE_STDLIB() \
  /* Assertions */ \
  POPULATE_STDLIB_ASSERTIONS(); \
  \
  /* Evaluation control */ \
  POPULATE_STDLIB_EVAL_CONTROL(); \
  \
  /* Manual runtime garbage collection */ \
  POPULATE_STDLIB_GC(); \
  \
  /* Debug value representation */ \
  POPULATE_STDLIB_SHOW(); \
  \
  /* Sequences */ \
  POPULATE_STDLIB_SEQUENCES(); \
  \
  /* Variables */ \
  POPULATE_STDLIB_VARIABLES(); \
  \
  /* Types */ \
  POPULATE_STDLIB_TYPES(); \
  \
  /* Strings */ \
  POPULATE_STDLIB_STRINGS(); \
  \
  /* Strings */ \
  POPULATE_STDLIB_ARRAYS(); \
  \
  /* Arithmetics */ \
  POPULATE_STDLIB_ARITHMETICS(); \
  /* Basic numeric functions */ \
  POPULATE_STDLIB_NUMERIC_FNS(); \
  /* Powers and logarithms */ \
  POPULATE_STDLIB_POW_N_LOG(); \
  /* Trigonometry */ \
  POPULATE_STDLIB_TRIGONOMETRY(); \
  \
  /* Comparisons */ \
  POPULATE_STDLIB_COMPARISONS(); \
  \
  /* Jumps */ \
  POPULATE_STDLIB_JUMPS();
