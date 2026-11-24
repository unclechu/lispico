#include <string.h>

#include "evaluate-copy.h"
#include "wipe-memory.h"

// Some helpers for copying values into evaluator GC-owned runtime allocated data.

// Copy a string into runtime storage, optionally preserving a read-only view.
static bool copy_runtime_string_impl(
  Interpreter *interpreter,
  String source,
  String *destination,
  bool preserve_immutable
) {
  if (source.length == SIZE_MAX) return false;
  if (preserve_immutable && source.immutable_ref) {
    if (source.length != 0 && source.data == NULL) return false;
    *destination = source;
    return true;
  }

  // Preserve an empty string represented by a NULL pointer.
  if (source.data == NULL) {
    if (source.length != 0) return false;
    *destination = (String) {
      .data = NULL,
      .length = 0,
      .base = NULL,
      .immutable_ref = false,
      .nul_terminated = false,
    };
    return true;
  }

  char *data = allocate_runtime_data(interpreter, source.length + 1u); // +1 for `\0`
  if (data == NULL) return false;

  if (source.length != 0) memcpy(data, source.data, source.length);
  data[source.length] = '\0';
  *destination = (String) {
    .data = data,
    .length = source.length,
    .base = data,
    .immutable_ref = false,
    .nul_terminated = true,
  };
  return true;
}

// Copy an array, optionally preserving read-only views of the array and its
// string elements. Forced copies always create independent writable storage.
static bool copy_runtime_array_impl(
  Interpreter *interpreter,
  Array source,
  Array *destination,
  bool preserve_immutable
) {
  if (preserve_immutable && source.immutable_ref) {
    if (source.length != 0 && source.data == NULL) return false;
    *destination = source;
    return true;
  }
  // Preserve an empty array represented by a NULL pointer.
  if (source.data == NULL) {
    if (source.length != 0) return false;
    *destination = (Array) {
      .element_type = source.element_type,
      .length = 0,
      .data = NULL,
      .base = NULL,
    };
    return true;
  }

  size_t element_size = 0;
  switch (source.element_type) {
    case TYPE_U32: element_size = sizeof(uint32_t); break;
    case TYPE_I32: element_size = sizeof(int32_t); break;
    case TYPE_F32: element_size = sizeof(float); break;
    case TYPE_STRING: element_size = sizeof(String); break;
    default: return false;
  }

  if (source.length != 0 && element_size > (SIZE_MAX / (size_t)source.length))
    return false;

  size_t size = (size_t)source.length * element_size;
  void *data = allocate_runtime_data(interpreter, size);
  if (data == NULL) return false;

  // `allocate_runtime_data` allocated at least one byte even for an empty
  // array. Initialize it.
  if (size == 0) wipe_memory(data, 1);
  else if (source.element_type != TYPE_STRING) memcpy(data, source.data, size);
  else {
    wipe_memory(data, size);
    for (uint32_t i = 0; i < source.length; ++i) {
      String string = ((const String *)source.data)[i];
      if (string.length == SIZE_MAX || (string.length != 0 && string.data == NULL))
        return false;
      // Preserve immutable elements only for ordinary copy-by-default binding.
      if (preserve_immutable && string.immutable_ref) {
        ((String *)data)[i] = string;
        continue;
      }
      if (string.data == NULL && string.length == 0) continue;

      String copied = {0};
      if (!copy_runtime_string_impl(interpreter, string, &copied, false)) return false;
      ((String *)data)[i] = copied;
    }
  }

  *destination = (Array) {
    .element_type = source.element_type,
    .length = source.length,
    .data = data,
    .base = data,
  };
  return true;
}

// Normal bindings keep immutable references and copy mutable data.
bool copy_runtime_string(Interpreter *interpreter, String source, String *destination) {
  return copy_runtime_string_impl(interpreter, source, destination, true);
}

bool copy_runtime_array(Interpreter *interpreter, Array source, Array *destination) {
  return copy_runtime_array_impl(interpreter, source, destination, true);
}

// `(copy)` always makes mutable, independent storage, including string elements.
bool copy_runtime_string_mutable(Interpreter *interpreter, String source, String *destination) {
  return copy_runtime_string_impl(interpreter, source, destination, false);
}

bool copy_runtime_array_mutable(Interpreter *interpreter, Array source, Array *destination) {
  return copy_runtime_array_impl(interpreter, source, destination, false);
}
