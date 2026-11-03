#include <stdlib.h>
#include <string.h>

#include "stdlib-env-var.h"
#include "stdlib-validation-utils.h"
#include "symbol-print.h"
#include "symbol-error.h"
#include "evaluate.h"
#include "wipe-memory.h"

// Validate an environment variable name to be a non-empty NUL-terminated string
// that is safe to be used for C’s `getenv()` call.
// Lispico strings have explicit lengths and can contain embedded NUL bytes as
// part of their normal value. Such strings will be rejected.
static bool validate_environment_variable_name(
  const Datum *arg,
  const char *fn_name,
  char (*fail_msg)[ERR_MSG_SIZE]
) {
  if (arg->kind != SYM_VARIABLE) {
    snprintf(
      *fail_msg,
      ERR_MSG_SIZE,
      "(%s) expects a string environment variable name, got kind: %s",
      fn_name,
      show_SymbolKind(arg->kind)
    );
    return false;
  }

  if (arg->as.value.type != TYPE_STRING) {
    snprintf(
      *fail_msg,
      ERR_MSG_SIZE,
      "(%s) expects a string environment variable name, got type: %s",
      fn_name,
      show_ValueType(arg->as.value.type)
    );
    return false;
  }

  String string = arg->as.value.as.string;

  // A variable name cannot be empty, contain '=', or contain a NUL byte
  // as part of the string value (within the defined string’s length).
  if (
    string.length == 0 ||
    string.data == NULL ||
    memchr(string.data, '\0', string.length) != NULL ||
    memchr(string.data, '=', string.length) != NULL ||
    string.data[string.length] != '\0'
  ) {
    snprintf(
      *fail_msg,
      ERR_MSG_SIZE,
      "(%s) invalid environment variable name",
      fn_name
    );
    return false;
  }

  return true;
}

// (env-var-has) — Check whether an environment variable is set (including empty values)
Datum std_env_var_has(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "env-var-has";

  if (!validate_argc(interpreter, fn_name, &fail_msg, 1, argc)) return return_value;

  if (!validate_environment_variable_name(&args[0], fn_name, &fail_msg)) goto fail;

  // getenv() returns NULL only when the variable is absent; "" is still set.
  bool is_set = getenv(args[0].as.value.as.string.data) != NULL;

  return_value = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = { .type = TYPE_U32, .as.u32 = is_set ? 1 : 0 },
  };

  return return_value;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (env-var-read) — Read an environment variable, optionally falling back on unset or empty
Datum std_env_var_read(Interpreter *interpreter, const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "env-var-read";

  if (argc < 1 || argc > 2) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects 1 or 2 arguments, got %zu", fn_name, argc);
    goto fail;
  }

  // Validate the optional default even if a nonempty environment value is set.
  String default_value = {0};
  if (argc == 2) {
    if (args[1].kind != SYM_VARIABLE) {
      snprintf(
        fail_msg,
        sizeof(fail_msg),
        "(%s) expects a string default value, got kind: %s",
        fn_name,
        show_SymbolKind(args[1].kind)
      );
      goto fail;
    }
    if (args[1].as.value.type != TYPE_STRING) {
      snprintf(
        fail_msg,
        sizeof(fail_msg),
        "(%s) expects a string default value, got type: %s",
        fn_name,
        show_ValueType(args[1].as.value.type)
      );
      goto fail;
    }

    default_value = args[1].as.value.as.string;
    if (
      default_value.length >= SIZE_MAX ||
      (default_value.length != 0 && default_value.data == NULL)
    ) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid default string value", fn_name);
      goto fail;
    }
  }

  if (!validate_environment_variable_name(&args[0], fn_name, &fail_msg)) goto fail;

  const char *environment_value = getenv(args[0].as.value.as.string.data);
  if (environment_value == NULL && argc == 1) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) environment variable “%s” is not set",
      fn_name,
      args[0].as.value.as.string.data
    );
    goto fail;
  }

  // Like Bash `${NAME:-default}`, the `default` applies when the value is unset
  // or empty. With no default, an existing empty value is returned as `""`.
  String result = {0};
  if (argc == 2 && (environment_value == NULL || environment_value[0] == '\0')) {
    result = default_value;
  } else {
    result = (String) { .data = environment_value, .length = strlen(environment_value) };
  }

  if (result.length >= SIZE_MAX) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) result string is too large", fn_name);
    goto fail;
  }

  // Copy the returned string into runtime-owned memory. `getenv()` points to
  // process environment storage, which must not become a Lispico value's data.
  char *data = allocate_runtime_data(interpreter, result.length + 1);
  if (data == NULL) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) runtime allocation failed (OOM or allocation capacity reached)",
      fn_name
    );
    goto fail;
  }

  // Copy by length so a default value may contain embedded NUL bytes.
  if (result.length != 0) memcpy(data, result.data, result.length);
  data[result.length] = '\0';

  return_value = (Datum) {
    .kind = SYM_VARIABLE,
    .as.value = {
      .type = TYPE_STRING,
      .as.string = { .data = data, .length = result.length },
    },
  };

  return return_value;

  fail:
    report_evaluation_failure(interpreter, fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}
