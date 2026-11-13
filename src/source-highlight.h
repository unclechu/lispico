#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "expression.h"

typedef enum {
  SOURCE_HIGHLIGHT_NONE = 0,
  SOURCE_HIGHLIGHT_BAT,
  SOURCE_HIGHLIGHT_SH,
} SourceHighlightMode;

// Convert a configured mode name into a source-rendering mode.
bool parse_source_highlight_mode(const char *name, SourceHighlightMode *mode);

// Show the source excerpt below a diagnostic that has already been printed.
// The bat preset silently falls back to the plain renderer; sh mode owns the
// complete excerpt and reports command failures without replacing its output.
// The file type is passed to bat as its language and to sh as context metadata.
void print_source_highlight(
  FILE *stream,
  const char *source,
  size_t source_length,
  const ExpressionSourceLocation *location,
  uint32_t lines_before,
  const char *file_type,
  SourceHighlightMode mode,
  const char *shell_command
);
