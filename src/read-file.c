#include "read-file.h"

// Read source code from a regular file or `/dev/stdin`.
//
// Will report final file size to `contents_size`.
bool read_file_contents(
  const char *path,
  char (*contents)[SOURCE_CAPACITY],
  size_t *contents_size // In bytes
) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    perror(path);
    return false;
  }

  *contents_size = fread(*contents, sizeof(char), sizeof(*contents), file);

  if (ferror(file)) {
    fprintf(stderr, "Failed to read file from: %s\n", path);
    goto fail;
  }

  // Check if the source exceeds the available buffer.
  if (*contents_size == sizeof(*contents)) {
    int extra = fgetc(file);

    if (extra != EOF) {
      fprintf(stderr, "File exceeds SOURCE_CAPACITY: %s\n", path);
      goto fail;
    }

    if (ferror(file)) {
      fprintf(stderr, "Failed to read file from: %s\n", path);
      goto fail;
    }
  }

  fclose(file);
  return true;

  fail:
    fclose(file);
    return false;
}
