#include "source-registry.h"

bool register_source_file(
  SourceRegistry *registry,
  const char *path,
  const char *source,
  size_t length,
  uint32_t *source_id
) {
  if (
    registry == NULL ||
    registry->files == NULL ||
    registry->count >= registry->capacity ||
    path == NULL ||
    (source == NULL && length != 0) ||
    source_id == NULL
  ) return false;

  *source_id = registry->count;
  registry->files[registry->count] = (SourceFile) {
    .path = path,
    .source = source,
    .length = length,
  };
  registry->count++;
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
