#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "symbol.h"

// Host-side NUL-terminated view. `owned` means that the host must release
// `data` through `clean_c_string()`, not through the evaluator or its GC.
typedef struct {
  const char *data;
  size_t length;
  bool owned;
} CString;

// Borrow a terminated Lispico string, or allocate a terminated copy when its
// visible end is not NUL-terminated. Embedded NUL bytes are preserved.
// Return false for invalid storage, length overflow, or allocation failure.
bool string_to_c_string(String source, CString *result);

// Wipe and free only caller-owned data. Borrowed data is never modified.
void clean_c_string(CString *string);
