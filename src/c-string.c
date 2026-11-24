#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "c-string.h"
#include "wipe-memory.h"

bool string_to_c_string(String source, CString *result) {
  if (result == NULL) return false;
  *result = (CString) {0};

  // A zero-initialized `String` is an empty string, not invalid storage.
  if (source.data == NULL) {
    if (source.length != 0) return false;
    *result = (CString) { .data = "", .length = 0 };
    return true;
  }

  if (source.length == SIZE_MAX) return false;

  if (source.nul_terminated) {
    *result = (CString) { .data = source.data, .length = source.length };
    return true;
  }

  // The extra byte is reserved for the C-string’s terminating `\0`.
  char *data = malloc(source.length + 1);
  if (data == NULL) return false;

  if (source.length != 0) memcpy(data, source.data, source.length);
  data[source.length] = '\0';
  *result = (CString) {
    .data = data,
    .length = source.length,
    .owned = true,
  };
  return true;
}

void clean_c_string(CString *string) {
  if (string == NULL) return;
  if (string->owned && string->data != NULL) {
    wipe_memory((void*)string->data, string->length + 1);
    free((void*)string->data);
  }
  *string = (CString) {0};
}
