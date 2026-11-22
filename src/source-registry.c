#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "source-registry.h"
#include "wipe-memory.h"

bool register_source_file(
  SourceRegistry *registry,
  const char *path,
  const char *source,
  size_t length,
  uint32_t *source_id
) {
  if (
    registry == NULL ||
    path == NULL ||
    (source == NULL && length != 0) ||
    source_id == NULL ||
    registry->count >= UINT32_MAX
  ) return false;

  if (registry->count >= registry->capacity) {
    if (!registry->dynamic) return false;

    uint32_t capacity =
      registry->capacity == 0
        ? SOURCE_REGISTRY_DYNAMIC_INITIAL_CAPACITY
        : registry->capacity;

    if (capacity <= registry->count) {
      uint32_t new_capacity = SOURCE_REGISTRY_DYNAMIC_CAPACITY_GROW(capacity);

      // Number overflow protection.
      //
      // Assuming that `SOURCE_REGISTRY_DYNAMIC_CAPACITY_GROW` formula is
      // sensible and can’t overflow twice, or reach bigger number after an
      // overflow.
      if (new_capacity < capacity) return false;

      capacity = new_capacity;
    }

    // Ensure that the next entry fits.
    if (capacity <= registry->count) return false;

#if SIZE_MAX < UINT64_MAX
    if (capacity > SIZE_MAX / sizeof(*registry->files)) return false;
#endif

    SourceFile *files = realloc(registry->files, (size_t)capacity * sizeof(*files));
    if (files == NULL) return false;

    registry->files = files;
    registry->capacity = capacity;
  }

  if (registry->files == NULL) return false;

  *source_id = registry->count;
  registry->files[registry->count] = (SourceFile) {
    .path = path,
    .source = source,
    .length = length,
  };

  registry->count++;

  return true;
}

bool register_owned_source_file(
  SourceRegistry *registry,
  char *path,
  char *source,
  size_t length,
  uint32_t *source_id
) {
  if (!register_source_file(registry, path, source, length, source_id)) return false;
  registry->files[*source_id].owned = true;
  return true;
}

const SourceFile* get_source_file(const SourceRegistry *registry, uint32_t source_id) {
  if (
    registry == NULL ||
    registry->files == NULL ||
    source_id >= registry->count
  ) return NULL;
  return &registry->files[source_id];
}

void clean_source_registry(SourceRegistry *registry) {
  if (registry == NULL) return;
  if (registry->files != NULL) {
    for (uint32_t i = 0; i < registry->count; ++i) {
      const SourceFile *file = &registry->files[i];
      if (!file->owned) continue;
      if (file->source != NULL) {
        wipe_memory((void*)file->source, file->length);
        free((void*)file->source);
      }
      wipe_memory((void*)file->path, strlen(file->path) + 1);
      free((void*)file->path);
    }
    if (registry->dynamic) free(registry->files);
  }
  *registry = (SourceRegistry) {0};
}

bool set_source_include_location(
  SourceRegistry *registry,
  uint32_t source_id,
  ExpressionSourceLocation location
) {
  if (
    registry == NULL ||
    registry->files == NULL ||
    source_id >= registry->count ||
    source_id == 0 ||
    location.source_id >= source_id
  ) return false;

  registry->files[source_id].has_include_location = true;
  registry->files[source_id].include_location = location;
  return true;
}

void print_source_include_trace(
  FILE *stream,
  const SourceRegistry *registry,
  uint32_t source_id
) {
  if (stream == NULL) return;
  // A parent is always registered before its child, so IDs strictly decrease.
  // This also bounds traversal even for malformed registry entries.
  while (source_id != 0) {
    const SourceFile *file = get_source_file(registry, source_id);
    if (file == NULL || !file->has_include_location) return;

    const ExpressionSourceLocation *site = &file->include_location;
    if (site->source_id >= source_id) return;
    const SourceFile *parent = get_source_file(registry, site->source_id);
    if (parent == NULL) return;

    fprintf(
      stream,
      "Included from %s:%" PRIu32 ":%" PRIu32 "\n",
      parent->path,
      site->line,
      site->column
    );
    source_id = site->source_id;
  }
}
