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

// Render the highlighter’s output with Lispico’s line-number gutter and arrow.
// Trust its source text; arrow placement still uses the original source bytes.
// Return false for an empty highlighter result or an invalid source location.
bool print_highlighted_source_location(
  FILE *stream,
  const char *source,
  size_t source_length,
  const ExpressionSourceLocation *location,
  uint32_t lines_before,
  const char *highlighted,
  size_t highlighted_length
);

// Write the original source with tabs expanded to four spaces, matching the
// built-in renderer. ASCII controls are escaped and CRLF becomes LF.
bool write_source_for_highlighting(
  FILE *stream,
  const char *source,
  size_t source_length
);
