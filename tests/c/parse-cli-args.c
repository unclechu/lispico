#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "utils/term-colors.h"
#include "utils/report.h"
#include "../../src/parse-cli-args.h"

// Each test owns only its `argv` pointer array. The strings themselves are
// borrowed by the parser, just as they are when supplied by the process.
typedef struct {
  const char *name;
  char *argv[32];
  CliParseStatus expected_status;
  const char *expected_error;
  const char *expected_source;
  bool expected_print_result;
  int expected_raw_args_start; // 0 means argc (no standalone `--`).
  size_t expected_definitions;
} ParseTest;

static void run_parse_test(
  const ParseTest *test,
  size_t *passed,
  size_t *failed
) {
  int argc = 0;
  while (argc < 32 && test->argv[argc] != NULL) ++argc;

  CliOptions options = {0};
  char error[ERR_MSG_SIZE] = {'\0'};
  CliParseStatus status = parse_cli_args(
    argc,
    test->argv,
    &options,
    error,
    sizeof(error)
  );

  bool ok = (status == test->expected_status);
  if (ok && status == CLI_PARSE_ERROR) {
    ok = (test->expected_error != NULL && strstr(error, test->expected_error) != NULL);
  } else if (ok && status == CLI_PARSE_OK) {
    const char *expected_source = test->expected_source;
    if (expected_source == NULL) expected_source = "/dev/stdin";

    int expected_raw_args_start = test->expected_raw_args_start;
    if (expected_raw_args_start == 0) expected_raw_args_start = argc;

    ok = (
      strcmp(options.source_file, expected_source) == 0 &&
      options.print_result == test->expected_print_result &&
      options.raw_args_start == expected_raw_args_start &&
      options.definition_count == test->expected_definitions &&
      error[0] == '\0'
    );
  } else if (ok && status == CLI_PARSE_HELP) {
    ok = (error[0] == '\0');
  }

  char actual[ERR_MSG_SIZE + 100] = {'\0'};
  snprintf(
    actual,
    sizeof(actual),
    "status=%d, source=%s, print=%d, raw=%d, definitions=%zu, error=%s",
    status,
    options.source_file != NULL ? options.source_file : "(null)",
    options.print_result,
    options.raw_args_start,
    options.definition_count,
    error
  );

  report_test(
    test->name,
    ok,
    "expected parser status and option fields",
    actual,
    passed,
    failed
  );
  clean_cli_args(&options);
}

static void check(
  const char *name,
  bool ok,
  size_t *passed,
  size_t *failed
) {
  report_test(name, ok, "true", ok ? "true" : "false", passed, failed);
}

// Check actual stored values, array gaps, borrowed string pointers and
// order-dependent freezing, not merely the number of parsed definitions.
static void test_definitions(size_t *passed, size_t *failed) {
  char *argv[] = {
    "lispico",
    "--var", "count", "i32:-123",
    "--var", "message", "s:Hello, world!",
    "--arr", "flags", "u32", "3", "true", "0", "false",
    "--arr", "names", "s", "3", "Alice", "", "--help",
    "--arr-new", "sparse", "s", "3",
    "--arr-set", "sparse", "1", "[middle, value]",
    "--freeze", "sparse",
    "-ps", "program.lisp",
    "--", "--debug", "two words",
  };
  int argc = (int)(sizeof(argv) / sizeof(argv[0]));

  CliOptions options = {0};
  char error[ERR_MSG_SIZE] = {'\0'};
  CliParseStatus status = parse_cli_args(
    argc,
    argv,
    &options,
    error,
    sizeof(error)
  );

  bool parsed = (status == CLI_PARSE_OK && options.definition_count == 5);
  check("mixed definition parsing", parsed, passed, failed);
  if (parsed) {
    const CliDefinition *count = &options.definitions[0];
    const CliDefinition *message = &options.definitions[1];
    const CliDefinition *flags = &options.definitions[2];
    const CliDefinition *names = &options.definitions[3];
    const CliDefinition *sparse = &options.definitions[4];

    check(
      "typed i32 scalar",
      count->kind == SYM_VARIABLE &&
      strcmp(count->name, "count") == 0 &&
      count->as.value.type == TYPE_I32 &&
      count->as.value.as.i32 == -123,
      passed,
      failed
    );
    check(
      "borrowed scalar string",
      message->kind == SYM_VARIABLE &&
      message->as.value.type == TYPE_STRING &&
      message->as.value.as.string.data == argv[6] + 2 && // +2 to skip `s:` prefix
      message->as.value.as.string.length == strlen("Hello, world!"),
      passed,
      failed
    );
    check(
      "numeric array with pseudo-booleans",
      flags->kind == SYM_ARRAY &&
      flags->as.array.element_type == TYPE_U32 &&
      flags->as.array.length == 3 &&
      ((const uint32_t *)flags->as.array.data)[0] == 1 &&
      ((const uint32_t *)flags->as.array.data)[1] == 0 &&
      ((const uint32_t *)flags->as.array.data)[2] == 0,
      passed,
      failed
    );
    const String *name_values = names->as.array.data;
    check(
      "literal string elements including empty and option-looking arguments",
      names->kind == SYM_ARRAY &&
      names->as.array.element_type == TYPE_STRING &&
      names->as.array.length == 3 &&
      name_values[0].data == argv[18] &&
      name_values[1].data == argv[19] &&
      name_values[1].length == 0 &&
      name_values[2].data == argv[20] &&
      strcmp(name_values[2].data, "--help") == 0,
      passed,
      failed
    );
    const String *sparse_values = sparse->as.array.data;
    check(
      "sparse string gaps remain NULL and supplied strings remain borrowed",
      sparse->kind == SYM_ARRAY &&
      sparse->frozen &&
      sparse->as.array.element_type == TYPE_STRING &&
      sparse->as.array.length == 3 &&
      sparse_values[0].data == NULL && sparse_values[0].length == 0 &&
      sparse_values[1].data == argv[28] &&
      sparse_values[1].length == strlen(argv[28]) &&
      sparse_values[2].data == NULL && sparse_values[2].length == 0,
      passed,
      failed
    );
    check(
      "grouped source, print flag and raw argument boundary",
      options.print_result &&
      strcmp(options.source_file, "program.lisp") == 0 &&
      options.raw_args_start == argc - 2 &&
      strcmp(argv[options.raw_args_start], "--debug") == 0,
      passed,
      failed
    );
  }

  clean_cli_args(&options);
  CliOptions empty = {0};
  check("cleanup wipes the options structure", memcmp(&options, &empty, sizeof(options)) == 0,
    passed, failed);
}

static void test_invalid_api(size_t *passed, size_t *failed) {
  CliOptions options = {0};
  char error[ERR_MSG_SIZE] = {'\0'};
  char *argv[] = { "lispico" };

  check(
    "reject negative argc",
    parse_cli_args(-1, argv, &options, error, sizeof(error)) == CLI_PARSE_ERROR,
    passed,
    failed
  );
  check(
    "reject NULL argv with positive argc",
    parse_cli_args(1, NULL, &options, error, sizeof(error)) == CLI_PARSE_ERROR,
    passed,
    failed
  );
  check(
    "reject NULL options",
    parse_cli_args(1, argv, NULL, error, sizeof(error)) == CLI_PARSE_ERROR,
    passed,
    failed
  );
  check(
    "allow zero argc and NULL argv",
    parse_cli_args(0, NULL, &options, error, sizeof(error)) == CLI_PARSE_OK &&
    options.raw_args_start == 0 && strcmp(options.source_file, "/dev/stdin") == 0,
    passed,
    failed
  );
  clean_cli_args(&options);
  clean_cli_args(NULL);
}

static void test_usage(size_t *passed, size_t *failed) {
  FILE *stream = tmpfile();
  if (stream == NULL) {
    check("usage can be written to a stream", false, passed, failed);
    return;
  }

  print_cli_usage(stream, "custom-lispico");
  rewind(stream);
  char buffer[4096] = {'\0'};
  size_t length = fread(buffer, 1, sizeof(buffer) - 1u, stream);
  fclose(stream);

  check(
    "usage documents array options",
    length > 0 &&
    strstr(buffer, "Usage: custom-lispico") != NULL &&
    strstr(buffer, "--arr NAME TYPE") != NULL &&
    strstr(buffer, "--arr-new NAME TYPE") != NULL &&
    strstr(buffer, "--arr-set NAME IDX VAL") != NULL &&
    strstr(buffer, "--freeze NAME") != NULL,
    passed,
    failed
  );
}

int main(void) {
  set_term_colors();

  const ParseTest cases[] = {
    { .name = "no arguments", .argv = {"lispico"}, .expected_status = CLI_PARSE_OK },
    { .name = "positional source", .argv = {"lispico", "script.lisp"},
      .expected_status = CLI_PARSE_OK, .expected_source = "script.lisp" },
    { .name = "stdin positional dash", .argv = {"lispico", "-"},
      .expected_status = CLI_PARSE_OK },
    { .name = "short source option", .argv = {"lispico", "-s", "script.lisp"},
      .expected_status = CLI_PARSE_OK, .expected_source = "script.lisp" },
    { .name = "long source option", .argv = {"lispico", "--source-file", "script.lisp"},
      .expected_status = CLI_PARSE_OK, .expected_source = "script.lisp" },
    { .name = "long equals source option", .argv = {"lispico", "--source-file=-file"},
      .expected_status = CLI_PARSE_OK, .expected_source = "-file" },
    { .name = "grouped print and source", .argv = {"lispico", "-ps", "script.lisp"},
      .expected_status = CLI_PARSE_OK, .expected_print_result = true,
      .expected_source = "script.lisp" },
    { .name = "grouped print and help", .argv = {"lispico", "-ph"},
      .expected_status = CLI_PARSE_HELP },
    { .name = "long help", .argv = {"lispico", "--help", "--bad"},
      .expected_status = CLI_PARSE_HELP },
    { .name = "short help", .argv = {"lispico", "-h"},
      .expected_status = CLI_PARSE_HELP },
    { .name = "options after positional source", .argv = {"lispico", "script.lisp", "-p"},
      .expected_status = CLI_PARSE_OK, .expected_source = "script.lisp",
      .expected_print_result = true },
    { .name = "raw arguments", .argv = {"lispico", "-p", "--", "--help", "--var"},
      .expected_status = CLI_PARSE_OK, .expected_print_result = true,
      .expected_raw_args_start = 3 },
    { .name = "raw arguments immediately after executable",
      .argv = {"lispico", "--", "script.lisp"}, .expected_status = CLI_PARSE_OK,
      .expected_raw_args_start = 2 },
    { .name = "empty raw arguments", .argv = {"lispico", "--"},
      .expected_status = CLI_PARSE_OK },
    { .name = "typed scalar", .argv = {"lispico", "--var", "count", "i32:123"},
      .expected_status = CLI_PARSE_OK, .expected_definitions = 1 },
    { .name = "empty scalar string", .argv = {"lispico", "--var", "text", "s:"},
      .expected_status = CLI_PARSE_OK, .expected_definitions = 1 },
    { .name = "dense numeric array", .argv = {"lispico", "--arr", "x", "u32", "3", "1", "2", "3"},
      .expected_status = CLI_PARSE_OK, .expected_definitions = 1 },
    { .name = "dense array consumes option-like values",
      .argv = {"lispico", "--arr", "x", "s", "3", "--", "--help", "-p"},
      .expected_status = CLI_PARSE_OK, .expected_definitions = 1 },
    { .name = "zero-length dense array", .argv = {"lispico", "--arr", "x", "u32", "0"},
      .expected_status = CLI_PARSE_OK, .expected_definitions = 1 },
    { .name = "zero-length sparse array", .argv = {"lispico", "--arr-new", "x", "s", "0"},
      .expected_status = CLI_PARSE_OK, .expected_definitions = 1 },
    { .name = "sparse array and assignment",
      .argv = {"lispico", "--arr-new", "x", "i32", "3", "--arr-set", "x", "2", "-7"},
      .expected_status = CLI_PARSE_OK, .expected_definitions = 1 },
    { .name = "assign into dense array",
      .argv = {"lispico", "--arr", "x", "u32", "1", "2", "--arr-set", "x", "0", "3"},
      .expected_status = CLI_PARSE_OK, .expected_definitions = 1 },
    { .name = "freeze scalar", .argv = {"lispico", "--var", "x", "u32:1", "--freeze", "x"},
      .expected_status = CLI_PARSE_OK, .expected_definitions = 1 },
    { .name = "freeze array", .argv = {"lispico", "--arr-new", "x", "u32", "2", "--freeze", "x"},
      .expected_status = CLI_PARSE_OK, .expected_definitions = 1 },
    { .name = "repeat freeze marker", .argv = {"lispico", "--var", "x", "u32:1", "--freeze", "x", "--freeze", "x"},
      .expected_status = CLI_PARSE_OK, .expected_definitions = 1 },

    { .name = "unknown long option", .argv = {"lispico", "--unknown"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "unknown option" },
    { .name = "unknown short option", .argv = {"lispico", "-q"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "unknown short option" },
    { .name = "short source must be last", .argv = {"lispico", "-sp", "file"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "must be last" },
    { .name = "grouped source before help is invalid", .argv = {"lispico", "-psh", "file"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "must be last" },
    { .name = "missing source for -s", .argv = {"lispico", "-s"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "requires a file path" },
    { .name = "missing source for -ps", .argv = {"lispico", "-ps"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "requires a file path" },
    { .name = "empty source path", .argv = {"lispico", "--source-file="},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "cannot be empty" },
    { .name = "duplicate source", .argv = {"lispico", "-s", "file", "another"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "more than once" },
    { .name = "missing variable operands", .argv = {"lispico", "--var", "x"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "requires NAME" },
    { .name = "missing variable type separator", .argv = {"lispico", "--var", "x", "123"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "requires TYPE:VALUE" },
    { .name = "unsupported variable type", .argv = {"lispico", "--var", "x", "bool:true"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "unsupported type" },
    { .name = "invalid numeric scalar", .argv = {"lispico", "--var", "x", "u32:-1"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "invalid or out-of-range" },
    { .name = "scalar numeric overflow", .argv = {"lispico", "--var", "x", "i32:2147483648"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "invalid or out-of-range" },
    { .name = "reserved empty definition", .argv = {"lispico", "--var", "empty", "s:value"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "invalid command-line definition name" },
    { .name = "empty definition name", .argv = {"lispico", "--var", "", "s:value"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "invalid command-line definition name" },
    { .name = "duplicate scalar", .argv = {"lispico", "--var", "x", "u32:1", "--var", "x", "u32:2"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "more than once" },
    { .name = "duplicate scalar and array", .argv = {"lispico", "--var", "x", "u32:1", "--arr-new", "x", "u32", "2"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "more than once" },
    { .name = "missing array length", .argv = {"lispico", "--arr", "x", "u32"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "requires NAME" },
    { .name = "missing dense elements", .argv = {"lispico", "--arr", "x", "u32", "3", "1"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "fewer elements" },
    { .name = "array invalid length", .argv = {"lispico", "--arr-new", "x", "u32", "-1"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "invalid length" },
    { .name = "array length overflow", .argv = {"lispico", "--arr", "x", "u32", "4294967296"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "invalid length" },
    { .name = "array unsupported type", .argv = {"lispico", "--arr-new", "x", "bool", "2"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "unsupported element type" },
    { .name = "array invalid element", .argv = {"lispico", "--arr", "x", "i32", "2", "1", "2.5"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "at index 1" },
    { .name = "array element overflow", .argv = {"lispico", "--arr", "x", "u32", "1", "4294967296"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "at index 0" },
    { .name = "array set before definition", .argv = {"lispico", "--arr-set", "x", "0", "1", "--arr-new", "x", "u32", "1"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "not previously defined" },
    { .name = "array set on scalar", .argv = {"lispico", "--var", "x", "u32:1", "--arr-set", "x", "0", "2"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "not previously defined" },
    { .name = "array set out of bounds", .argv = {"lispico", "--arr-new", "x", "u32", "2", "--arr-set", "x", "2", "1"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "out of bounds" },
    { .name = "array set negative index", .argv = {"lispico", "--arr-new", "x", "u32", "2", "--arr-set", "x", "-1", "1"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "out of bounds" },
    { .name = "array set malformed index", .argv = {"lispico", "--arr-new", "x", "u32", "2", "--arr-set", "x", "1x", "1"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "out of bounds" },
    { .name = "array set invalid value", .argv = {"lispico", "--arr-new", "x", "u32", "1", "--arr-set", "x", "0", "-1"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "invalid element value" },
    { .name = "array set after freeze", .argv = {"lispico", "--arr-new", "x", "u32", "1", "--freeze", "x", "--arr-set", "x", "0", "1"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "is frozen" },
    { .name = "freeze before definition", .argv = {"lispico", "--freeze", "x", "--var", "x", "u32:1"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "cannot freeze undefined" },
    { .name = "freeze nonexistent built-in", .argv = {"lispico", "--freeze", "argc"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "cannot freeze undefined" },
    { .name = "missing freeze name", .argv = {"lispico", "--freeze"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "requires NAME" },
    { .name = "missing array set values", .argv = {"lispico", "--arr-set", "x", "0"},
      .expected_status = CLI_PARSE_ERROR, .expected_error = "requires NAME" },
  };

  size_t passed = 0;
  size_t failed = 0;

  print_test_header("parse_cli_args() argument and error cases");
  for (size_t i = 0; i < (sizeof(cases) / sizeof(cases[0])); ++i)
    run_parse_test(&cases[i], &passed, &failed);

  print_test_header("parse_cli_args() stored definitions and ownership");
  test_definitions(&passed, &failed);

  print_test_header("parse_cli_args() API validation and usage");
  test_invalid_api(&passed, &failed);
  test_usage(&passed, &failed);

  return summary_report("CLI argument parser", passed, failed);
}
