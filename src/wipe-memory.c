#include "wipe-memory.h"

// Explicitly wipe memory (in case there’s sensitive data is present).
//
// `volatile` guards from optimizations.
void wipe_memory(void *data, size_t size) {
  volatile unsigned char *p = data;
  while (size-- > 0) *p++ = 0;
}
