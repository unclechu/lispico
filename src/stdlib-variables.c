#include <string.h>

#include "stdlib-variables.h"
#include "stdlib-validation-utils.h"
#include "symbol-error.h"
#include "wipe-memory.h"
#include "evaluate.h"

// Extract variable name from a `Datum`
bool get_variable_name(const Datum *datum, char (*name)[SYMBOL_NAME_SIZE]) {
  if (datum->kind != SYM_VARIABLE || datum->as.value.type != TYPE_STRING) return false;
  String string = datum->as.value.as.string;

  if (
    string.data == NULL ||
    string.length == 0 ||
    string.length >= sizeof(*name) ||
    // The string length must not include string terminator
    memchr(string.data, '\0', string.length) != NULL
  ) return false;

  memcpy(*name, string.data, string.length);
  (*name)[string.length] = '\0';
  return true;
}

// (var) — Define a new variable, inferring its type from the initial value
Datum std_var(const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "var";
  char symbol_name[SYMBOL_NAME_SIZE] = {'\0'};

  if (!validate_argc(fn_name, &fail_msg, 2, argc)) return return_value;

  if (!get_variable_name(&args[0], &symbol_name)) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid variable name", fn_name);
    goto fail;
  }

  if (args[1].kind != SYM_VARIABLE) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects a scalar value", fn_name);
    goto fail;
  }

  // TODO: Add support for arrays
  switch (args[1].as.value.type) {
    case TYPE_U32:
    case TYPE_I32:
    case TYPE_F32:
    case TYPE_STRING:
      break;
    default:
      report_unexpected_value_type(fn_name, &fail_msg, args[1].as.value.type);
      return return_value;
  }

  Value value = args[1].as.value;

  if (value.type == TYPE_STRING) {
    String string = value.as.string;

    if (string.length == SIZE_MAX || (string.length != 0 && string.data == NULL)) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid string value", fn_name);
      goto fail;
    }

    // String variables own independent writable storage.
    // Reserve one extra byte for a convenient '\0' terminator.
    char *data = allocate_runtime_data(string.length + 1);
    if (data == NULL) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) runtime allocation failed", fn_name);
      goto fail;
    }

    if (string.length != 0) memcpy(data, string.data, string.length);
    data[string.length] = '\0';

    value.as.string.data = data;
  }

  // Important: Allocate the string before calling `symbol_define`, rather than
  // using that replacement verbatim after symbol creation. Otherwise an
  // allocation failure could leave a partially initialized symbol in the table.
  Symbol *symbol = symbol_define(symbol_name, SYM_VARIABLE);

  if (symbol == NULL) {
    extract_error_message_from_symbol_define_error(&fail_msg, symbol_name);
    goto fail;
  }

  symbol->as.variable = value;

  return_value = (Datum) { .kind = SYM_VARIABLE, .as.value = value };
  return return_value;

  fail:
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}

// (set) — Overwrite an existing variable
Datum std_set(const Datum *args, size_t argc) {
  Datum return_value = { .kind = SYM_EMPTY };
  char fail_msg[ERR_MSG_SIZE] = {'\0'};
  const char fn_name[] = "set";
  char symbol_name[SYMBOL_NAME_SIZE] = {'\0'};

  if (!validate_argc(fn_name, &fail_msg, 2, argc)) return return_value;

  if (!get_variable_name(&args[0], &symbol_name)) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid variable name", fn_name);
    goto fail;
  }

  if (args[1].kind != SYM_VARIABLE) {
    snprintf(fail_msg, sizeof(fail_msg), "(%s) expects a scalar value", fn_name);
    goto fail;
  }

  Symbol *symbol = symbol_lookup(symbol_name);

  if (symbol == NULL) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) variable “%s” is not defined",
      fn_name,
      symbol_name
    );
    goto fail;
  }

  if (symbol->kind != SYM_VARIABLE) {
    snprintf(
      fail_msg,
      sizeof(fail_msg),
      "(%s) symbol “%s” is not a variable",
      fn_name,
      symbol_name
    );
    goto fail;
  }

  if (symbol->as.variable.type != args[1].as.value.type) {
    snprintf(
      fail_msg, sizeof(fail_msg),
      "(%s) variable type mismatch: expected %s, got %s",
      fn_name,
      show_ValueType(symbol->as.variable.type),
      show_ValueType(args[1].as.value.type)
    );
    goto fail;
  }

  // TODO: add support for arrays
  switch (args[1].as.value.type) {
    case TYPE_U32:
    case TYPE_I32:
    case TYPE_F32:
    case TYPE_STRING:
      break;
    default:
      report_unexpected_value_type(fn_name, &fail_msg, args[1].as.value.type);
      return return_value;
  }

  if (args[1].as.value.type == TYPE_STRING) {
    String destination = symbol->as.variable.as.string;
    String source = args[1].as.value.as.string;

    if (
      destination.data == NULL ||
      (source.length != 0 && source.data == NULL) ||
      source.length > destination.length
    ) {
      snprintf(fail_msg, sizeof(fail_msg), "(%s) invalid string or out-of-bounds write", fn_name);
      goto fail;
    }

    // Overwrite without changing the string length or allocating memory.
    // `memmove` is safe even if source and destination overlap.
    char *data = (char *)destination.data;
    if (source.length != 0) memmove(data, source.data, source.length);

    // Clear (`\0`-fill) any unused bytes after the new content.
    memset(data + source.length, '\0', destination.length - source.length);
  } else {
    symbol->as.variable = args[1].as.value;
  }

  return_value = (Datum) { .kind = SYM_VARIABLE, .as.value = symbol->as.variable };
  return return_value;

  fail:
    report_evaluation_failure(fail_msg);
    wipe_memory(fail_msg, sizeof(fail_msg));
    return return_value;
}
