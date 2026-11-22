#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../src/parse.h"
#include "../../src/storage.h"

static void write_file(const char *path, const void *contents, size_t length) {
  FILE *file = fopen(path, "wb");
  assert(file != NULL);
  assert(fwrite(contents, 1, length, file) == length);
  assert(fclose(file) == 0);
}

static void write_text(const char *path, const char *contents) {
  write_file(path, contents, strlen(contents));
}

// Verify that a failed expansion leaves expression storage reusable, regardless
// of whether its included source buffers remain owned by a source registry.
static void check_failure(
  Interpreter *interpreter,
  SourceRegistry *registry,
  const char *main_path,
  const char *source,
  const char *expected,
  const char *expected_path
) {
  char error[ERR_MSG_SIZE] = {0};
  ParseDiagnostic diagnostic = {0};
  assert(!parse_source_with_includes_diagnostic(
    interpreter,
    source,
    strlen(source),
    main_path,
    registry,
    &error,
    &diagnostic
  ));
  assert(strstr(error, expected) != NULL);
  assert(interpreter->expression_count == 0);
  assert(interpreter->expression_allocation_count == 0);
  assert(diagnostic.has_location);
  if (expected_path != NULL) assert(strstr(diagnostic.path, expected_path) != NULL);
}

int main(void) {
  static Expression expressions[256] = {0};
  static ExpressionSourceLocation locations[256] = {0};
  static ExpressionAllocation allocations[256] = {0};
  static Symbol symbols[128] = {0};
  static EvalRuntimeAllocation runtime[128] = {0};
  static uint32_t deferred[64] = {0};
  InterpreterStorage storage = {
    .expressions = expressions,
    .expression_capacity = 256,
    .expression_source_locations = locations,
    .expression_source_location_capacity = 256,
    .expression_allocations = allocations,
    .expression_allocation_capacity = 256,
    .symbols = symbols,
    .symbol_capacity = 128,
    .runtime_allocations = runtime,
    .runtime_allocation_capacity = 128,
    .deferred_expression_indices = deferred,
    .deferred_expression_capacity = 64,
  };
  Interpreter interpreter = {0};
  assert(configure_interpreter_storage(&interpreter, &storage));

  char directory[] = "/tmp/lispico-include-robustness-XXXXXX";
  assert(mkdtemp(directory) != NULL);
  char main_path[PATH_MAX];
  char target_path[PATH_MAX];
  snprintf(main_path, sizeof(main_path), "%s/main.lisp", directory);
  snprintf(target_path, sizeof(target_path), "%s/target.lisp", directory);
  write_text(main_path, "(include \"target.lisp\")");
  write_text(target_path, "7\n");

  const char *main_source = "(include \"target.lisp\")";
  char error[ERR_MSG_SIZE] = {0};
  ParseDiagnostic diagnostic = {0};

  // A fixed registry can hold the entry source, but has no slot for includes.
  SourceFile fixed_files[1] = {0};
  SourceRegistry fixed = { .files = fixed_files, .capacity = 1 };
  uint32_t source_id = UINT32_MAX;
  assert(register_source_file(
    &fixed, main_path, main_source, strlen(main_source), &source_id
  ));
  assert(source_id == 0);
  check_failure(
    &interpreter,
    &fixed,
    main_path,
    main_source,
    "Unable to register include source",
    NULL
  );
  assert(fixed.count == 1);
  clean_source_registry(&fixed);

  // With no source map, expansion works without a source registry at all.
  InterpreterStorage no_map_storage = storage;
  no_map_storage.expression_source_locations = NULL;
  no_map_storage.expression_source_location_capacity = 0;
  Interpreter no_map = {0};
  assert(configure_interpreter_storage(&no_map, &no_map_storage));
  assert(parse_source_with_includes_diagnostic(
    &no_map,
    main_source,
    strlen(main_source),
    main_path,
    NULL,
    &error,
    &diagnostic
  ));
  assert(no_map.expression_count == 1);
  clean_expressions(&no_map);

  // An include that exactly fills SOURCE_CAPACITY succeeds; one extra byte
  // fails without leaving partially parsed expressions or allocations.
  char *large = malloc(SOURCE_CAPACITY + 1u);
  assert(large != NULL);
  memset(large, ' ', SOURCE_CAPACITY + 1u);
  large[0] = '1';
  write_file(target_path, large, SOURCE_CAPACITY);
  assert(parse_source_with_includes(
    &no_map, main_source, strlen(main_source), main_path, NULL, &error
  ));
  assert(no_map.expression_count == 1);
  clean_expressions(&no_map);
  write_file(target_path, large, SOURCE_CAPACITY + 1u);
  check_failure(&no_map, NULL, main_path, main_source, "SOURCE_CAPACITY", NULL);
  free(large);

  // Reject embedded NUL bytes, and leave the parser ready for a later source.
  const char binary[] = { '1', '\0', '2' };
  write_file(target_path, binary, sizeof(binary));
  check_failure(&no_map, NULL, main_path, main_source, "null byte", NULL);

  // The parser must clean up expressions already allocated before a failure.
  write_text(target_path, "\"first\" \"second\" (+ 1");
  check_failure(&no_map, NULL, main_path, main_source, "Unterminated list", NULL);

  // Check both expression and allocation exhaustion during included parsing.
  write_text(target_path, "1 2 3 4\n");
  InterpreterStorage small_storage = no_map_storage;
  small_storage.expression_capacity = 2;
  Interpreter small = {0};
  assert(configure_interpreter_storage(&small, &small_storage));
  check_failure(&small, NULL, main_path, main_source, "Expression capacity exceeded", NULL);

  write_text(target_path, "\"first\" \"second\"\n");
  small_storage.expression_capacity = 256;
  small_storage.expression_allocation_capacity = 1;
  Interpreter small_allocations = {0};
  assert(configure_interpreter_storage(&small_allocations, &small_storage));
  check_failure(
    &small_allocations,
    NULL,
    main_path,
    main_source,
    "Unable to allocate expression text",
    NULL
  );

  // A successful parse after a failure must have no residual expressions.
  write_text(target_path, "9\n");
  assert(parse_source_with_includes(
    &small,
    main_source,
    strlen(main_source),
    main_path,
    NULL,
    &error
  ));
  assert(small.expression_count == 1);
  clean_expressions(&small);

  // A relatively deep, non-circular chain can expand into a single expression.
  // Every included file is loaded independently, even if many files are used.
  enum { CHAIN_LENGTH = 48 };
  char chain_path[PATH_MAX];
  char text[80];
  for (unsigned i = 0; i < CHAIN_LENGTH; ++i) {
    snprintf(chain_path, sizeof(chain_path), "%s/chain-%02u.lisp", directory, i);
    if (i + 1u < CHAIN_LENGTH)
      snprintf(text, sizeof(text), "(include \"chain-%02u.lisp\")\n", i + 1u);
    else
      snprintf(text, sizeof(text), "42\n");
    write_text(chain_path, text);
  }

  const char *chain_source = "(include \"chain-00.lisp\")";
  SourceRegistry registry = { .dynamic = true };
  source_id = UINT32_MAX;
  assert(register_source_file(
    &registry, main_path, chain_source, strlen(chain_source), &source_id
  ));
  assert(parse_source_with_includes_diagnostic(
    &interpreter,
    chain_source,
    strlen(chain_source),
    main_path,
    &registry,
    &error,
    &diagnostic
  ));
  assert(interpreter.expression_count == 1);
  assert(registry.count == CHAIN_LENGTH + 1u);
  assert(registry.capacity >= registry.count);
  assert(registry.files[CHAIN_LENGTH].has_include_location);
  clean_expressions(&interpreter);
  clean_source_registry(&registry);

  // A deep parsing failure retains the actual innermost source location and
  // an include trace even when source mapping is disabled.
  snprintf(chain_path, sizeof(chain_path), "%s/chain-%02u.lisp", directory, CHAIN_LENGTH - 1);
  write_text(chain_path, "(+ 1\n");
  assert(!parse_source_with_includes_diagnostic(
    &no_map,
    chain_source,
    strlen(chain_source),
    main_path,
    NULL,
    &error,
    &diagnostic
  ));
  assert(no_map.expression_count == 0);
  assert(no_map.expression_allocation_count == 0);
  assert(diagnostic.has_location);
  assert(strstr(diagnostic.path, "chain-47.lisp") != NULL);
  assert(strstr(error, "Unterminated list") != NULL);
  assert(strstr(diagnostic.include_trace, "chain-46.lisp") != NULL);
  assert(diagnostic.include_trace_truncated);
  assert(diagnostic.include_trace[strlen(diagnostic.include_trace) - 1u] == '\n');

  // Nested failure after growing the registry: release all included buffers.
  registry = (SourceRegistry) { .dynamic = true };
  source_id = UINT32_MAX;
  assert(register_source_file(
    &registry, main_path, chain_source, strlen(chain_source), &source_id
  ));
  check_failure(&interpreter, &registry, main_path, chain_source, "Unterminated list", NULL);
  assert(registry.count == CHAIN_LENGTH + 1u);
  clean_source_registry(&registry);

  // The original file-aware API still supports an absolute include path.
  write_text(target_path, "5\n");
  char absolute_source[PATH_MAX + 32];
  snprintf(absolute_source, sizeof(absolute_source), "(include \"%s\")", target_path);
  assert(parse_source_with_includes(
    &no_map,
    absolute_source,
    strlen(absolute_source),
    main_path,
    NULL,
    &error
  ));
  assert(no_map.expression_count == 1);
  clean_expressions(&no_map);

  for (unsigned i = 0; i < CHAIN_LENGTH; ++i) {
    snprintf(chain_path, sizeof(chain_path), "%s/chain-%02u.lisp", directory, i);
    assert(unlink(chain_path) == 0);
  }
  assert(unlink(main_path) == 0);
  assert(unlink(target_path) == 0);
  assert(rmdir(directory) == 0);

  puts("Parser include robustness tests passed");
  return 0;
}
