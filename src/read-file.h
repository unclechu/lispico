#pragma once

#include <stdio.h>

#include "constants.h"

// Read contents of a file with undetermined size.
//
// Will work equally for a regular file or a stream like `/dev/stdin`.
bool read_file_contents(
  const char *path,
  char (*contents)[SOURCE_CAPACITY],
  size_t *contents_size // In bytes
);
