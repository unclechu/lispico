#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Source text and its path are owned by the host and must remain valid while
// diagnostics may reference them. Neither is part of the expression program.
typedef struct {
  const char *path;
  const char *source;
  size_t length;
} SourceFile;

// Optional host-owned table for resolving expression source IDs to files.
// Entries are registered in order, starting with source ID 0 for the main file.
typedef struct {
  SourceFile *files;
  uint32_t count;
  uint32_t capacity;
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
