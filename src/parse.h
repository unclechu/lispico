#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "expression.h"
#include "source-registry.h"

// Parse source into the global `expressions` and `expression_allocations` tables.
// Clears previous expressions first; leaves both tables empty on failure.
bool parse_source(
  Interpreter *interpreter,
  const char *source,
  size_t length,
  char (*error)[ERR_MSG_SIZE]
);

// Parse with recursive, static `(include "PATH")` expansion. Paths are resolved
// relative to the containing file; a `NULL` or `/dev/stdin` main path uses cwd.
// When the expression source map is enabled, registry must contain the main
// source as entry 0, and included source buffers are retained in the registry
// until `clean_source_registry()`. With source mapping disabled, registry may
// be `NULL` and included buffers are discarded as soon as they are parsed.
bool parse_source_with_includes(
  Interpreter *interpreter,
  const char *source,
  size_t length,
  const char *source_path,
  SourceRegistry *registry,
  char (*error)[ERR_MSG_SIZE]
);

// Structured parsing failure data for source excerpts and include traces.
// `location` refers to the failing source occurrence when available. The
// include trace is formatted while recursive parser frames are still alive,
// so it works even when source mapping and source retention are disabled.
typedef struct {
  bool has_location;
  ExpressionSourceLocation location;
  char path[ERR_MSG_SIZE]; // Snapshot, valid even after unmapped includes are freed.
  char include_trace[ERR_MSG_SIZE];
} ParseDiagnostic;

// Like `parse_source_with_includes()`, but additionally capture a structured
// failure location and the include chain. The source registry is still optional
// when source maps are disabled.
bool parse_source_with_includes_diagnostic(
  Interpreter *interpreter,
  const char *source,
  size_t length,
  const char *source_path,
  SourceRegistry *registry,
  char (*error)[ERR_MSG_SIZE],
  ParseDiagnostic *diagnostic
);
