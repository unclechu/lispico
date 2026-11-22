#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "expression.h"

// Source text and its path must remain valid while diagnostics may reference
// them. Main-source buffers are host-owned; included buffers can be owned by
// the registry. Neither is part of the expression program.
typedef struct {
  const char *path;
  const char *source;
  size_t length;
  bool owned; // Included source: registry owns its path and source buffers.

  // The include directive that brought this occurrence into the program.
  // Root sources have `has_include_location == false`. The parent ID and
  // location are retained only for diagnostics, never for evaluation.
  bool has_include_location;
  ExpressionSourceLocation include_location;
} SourceFile;

// Optional table for resolving expression source IDs to files.
// Entries are registered in order, starting with source ID 0 for the main file.
typedef struct {
  SourceFile *files;
  uint32_t count;
  uint32_t capacity;
  bool dynamic; // Grow the file table as needed; free it during cleanup.
} SourceRegistry;

// Register an existing source buffer without copying it. Returns false if the
// registry is invalid or full, or if the source buffer is invalid.
bool register_source_file(
  SourceRegistry *registry,
  const char *path,
  const char *source,
  size_t length,
  uint32_t *source_id
);

// Returns NULL when the source ID is not registered.
const SourceFile* get_source_file(const SourceRegistry *registry, uint32_t source_id);

// Transfer ownership of the included file’s path and source to the registry.
// On failure, the caller retains ownership of both buffers.
bool register_owned_source_file(
  SourceRegistry *registry,
  char *path,
  char *source,
  size_t length,
  uint32_t *source_id
);

// Release owned included sources and, if dynamic, the file table itself.
// Source buffers registered through `register_source_file()` remain host-owned.
void clean_source_registry(SourceRegistry *registry);

// Attach the parent include site to a previously registered source occurrence.
// Repeated includes retain distinct parent chains, even for the same path.
bool set_source_include_location(
  SourceRegistry *registry,
  uint32_t source_id,
  ExpressionSourceLocation location
);

// Print the chain from the immediate includer back to the entry source.
void print_source_include_trace(
  FILE *stream,
  const SourceRegistry *registry,
  uint32_t source_id
);
