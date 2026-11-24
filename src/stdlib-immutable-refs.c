#include <stdio.h>

#include "stdlib-immutable-refs.h"
#include "stdlib-validation-utils.h"
#include "evaluate.h"
#include "evaluate-copy.h"
#include "wipe-memory.h"

// An immutable reference is a read-only view, not immutable underlying storage.
// Mark only this descriptor: string elements of an array have independent flags.
Datum std_immutable_ref(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum result = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "immutable-ref";

  if (!validate_argc(interpreter, fn_name, &fail_msg, 1, argc)) return result;

  result = args[0];
  if (result.kind == SYM_VARIABLE && result.as.value.type == TYPE_STRING) {
    String *string = &result.as.value.as.string;
    if (string->data == NULL && string->length != 0) goto invalid;
    string->immutable_ref = true;
    return result;
  }
  if (result.kind == SYM_ARRAY) {
    Array *array = &result.as.array;
    if (array->data == NULL && array->length != 0) goto invalid;
    array->immutable_ref = true;
    return result;
  }

  invalid:
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects a string or array", fn_name);
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return (Datum) { .kind = SYM_EMPTY };
}

static bool is_immutable_ref(const Datum *value) {
  if (value->kind == SYM_ARRAY) return value->as.array.immutable_ref;
  return (
    value->kind == SYM_VARIABLE &&
    value->as.value.type == TYPE_STRING &&
    value->as.value.as.string.immutable_ref
  );
}

// Check only the outer string/array descriptor, not an array’s elements.
Datum std_is_immutable_ref(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum result = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  if (!validate_argc(interpreter, "is-immutable-ref", &fail_msg, 1, argc)) return result;
  return (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = { .type = TYPE_U32, .as.u32 = is_immutable_ref(&args[0]) ? 1u : 0u },
  };
}

// Include immutable string elements even if the containing array is mutable.
Datum std_has_immutable_refs(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum result = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  if (!validate_argc(interpreter, "has-immutable-refs", &fail_msg, 1, argc)) return result;

  bool found = is_immutable_ref(&args[0]);
  if (!found && args[0].kind == SYM_ARRAY) {
    Array array = args[0].as.array;
    if (array.element_type == TYPE_STRING && array.data != NULL) {
      const String *strings = array.data;
      for (uint32_t i = 0; i < array.length; ++i) {
        if (strings[i].immutable_ref) {
          found = true;
          break;
        }
      }
    }
  }
  return (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = { .type = TYPE_U32, .as.u32 = found ? 1u : 0u },
  };
}

// (copy) — Make an independent, mutable copy of a string or array.
// String-array elements are copied deeply, including immutable references.
Datum std_copy(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum result = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "copy";

  if (!validate_argc(interpreter, fn_name, &fail_msg, 1, argc)) return result;

  if (args[0].kind == SYM_VARIABLE && args[0].as.value.type == TYPE_STRING) {
    String string = {0};
    if (!copy_runtime_string_mutable(
      interpreter,
      args[0].as.value.as.string,
      &string
    )) goto allocation_failed;

    result = args[0];
    result.as.value.as.string = string;
    return result;
  }

  if (args[0].kind == SYM_ARRAY) {
    Array array = {0};
    if (!copy_runtime_array_mutable(interpreter, args[0].as.array, &array))
      goto allocation_failed;

    result = args[0];
    result.as.array = array;
    return result;
  }

  snprintf(fail_msg, sizeof(fail_msg), "(%s) expects a string or array", fn_name);
  goto fail;

  allocation_failed:
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) failed to copy value (invalid storage, OOM, or allocation capacity reached)",
      fn_name
    );
    goto fail;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return result;
}
