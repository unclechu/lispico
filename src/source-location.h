#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "expression.h"

// Print the failing source line and up to `lines_before` preceding lines.
// Return false if the source location is outside the supplied source buffer.
bool print_source_location(
  FILE *stream,
  const char *source,
  size_t source_length,
  const ExpressionSourceLocation *location,
  uint32_t lines_before
);
