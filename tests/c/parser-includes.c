#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../src/evaluate.h"
#include "../../src/parse.h"
#include "../../src/storage.h"
#include "../../src/source-location.h"
#include "../../src/symbol.h"

static void write_source(const char *path, const char *source) {
  FILE *file = fopen(path, "wb");
  assert(file != NULL);
  size_t length = strlen(source);
  assert(fwrite(source, 1, length, file) == length);
  assert(fclose(file) == 0);
}

static void reset_interpreter(Interpreter *interpreter) {
  clean_evaluation(interpreter);
  clean_symbols(interpreter);
  clean_expressions(interpreter);
}

int main(void) {
  static Expression expressions[512] = {0};
  static ExpressionSourceLocation locations[512] = {0};
  static ExpressionAllocation expression_allocations[256] = {0};
  static Symbol symbols[1024] = {0};
  static EvalRuntimeAllocation runtime_allocations[512] = {0};
  static uint32_t deferred_expressions[64] = {0};
  InterpreterStorage storage = {
    .expressions = expressions,
    .expression_capacity = 512,
    .expression_source_locations = locations,
    .expression_source_location_capacity = 512,
    .expression_allocations = expression_allocations,
    .expression_allocation_capacity = 256,
    .symbols = symbols,
    .symbol_capacity = 1024,
    .runtime_allocations = runtime_allocations,
    .runtime_allocation_capacity = 512,
    .deferred_expression_indices = deferred_expressions,
    .deferred_expression_capacity = 64,
  };
  Interpreter interpreter = {0};
  assert(configure_interpreter_storage(&interpreter, &storage));

  char directory[] = "/tmp/lispico-parser-includes-XXXXXX";
  assert(mkdtemp(directory) != NULL);
  char main_path[PATH_MAX];
  char shared_path[PATH_MAX];
  char numbers_path[PATH_MAX];
  char nested_path[PATH_MAX];
  char nested_dir[PATH_MAX];
  char statements_path[PATH_MAX];
  char empty_path[PATH_MAX];
  char bad_path[PATH_MAX];
  char alias_path[PATH_MAX];
  char relay_path[PATH_MAX];
  snprintf(main_path, sizeof(main_path), "%s/main.lisp", directory);
  snprintf(shared_path, sizeof(shared_path), "%s/shared.lisp", directory);
  snprintf(numbers_path, sizeof(numbers_path), "%s/numbers.lisp", directory);
  snprintf(nested_path, sizeof(nested_path), "%s/sub/more.lisp", directory);
  snprintf(nested_dir, sizeof(nested_dir), "%s/sub", directory);
  snprintf(statements_path, sizeof(statements_path), "%s/statements.lisp", directory);
  snprintf(empty_path, sizeof(empty_path), "%s/empty.lisp", directory);
  snprintf(bad_path, sizeof(bad_path), "%s/bad.lisp", directory);
  snprintf(alias_path, sizeof(alias_path), "%s/alias.lisp", directory);
  snprintf(relay_path, sizeof(relay_path), "%s/sub/relay.lisp", directory);
  assert(mkdir(nested_dir, 0700) == 0);

  const char *main_source =
    "(include \"shared.lisp\")\n"
    "(assert (== (+ (include \"numbers.lisp\") 5) 15))\n"
    "(do (local \"x\" 1) (include \"statements.lisp\") (assert (== x 5)))\n"
    "(include \"empty.lisp\")\n"
    "(include \"shared.lisp\")\n"
    "(assert (== (square 3) 9))\n";
  write_source(main_path, main_source);
  write_source(shared_path, "(fn \"square\" (args \"n\") (* n n))\n");
  write_source(numbers_path, "1\n2\n(include \"sub/more.lisp\")\n");
  write_source(nested_path, "3\n4\n");
  write_source(statements_path, "(set \"x\" (+ x 4))\n");
  write_source(empty_path, "; contributes zero expressions\n");
  write_source(bad_path, "(+ 1 2\n");
  write_source(relay_path, "(include \"../bad.lisp\")\n");

  char error[ERR_MSG_SIZE] = {0};
  SourceRegistry registry = { .dynamic = true };
  uint32_t source_id = UINT32_MAX;
  assert(register_source_file(&registry, main_path, main_source, strlen(main_source), &source_id));
  assert(source_id == 0);
  assert(parse_source_with_includes(
    &interpreter,
    main_source,
    strlen(main_source),
    main_path,
    &registry,
    &error
  ));
  assert(registry.count == 7); // main, shared, numbers, nested, statements, empty, shared again
  const SourceFile *nested_file = get_source_file(&registry, 3);
  assert(nested_file != NULL && nested_file->has_include_location);
  assert(nested_file->include_location.source_id == 2);
  assert(nested_file->include_location.line == 3);
  assert(nested_file->include_location.column == 1);
  const SourceFile *repeated = get_source_file(&registry, 6);
  assert(repeated != NULL && repeated->has_include_location);
  assert(repeated->include_location.source_id == 0);
  assert(repeated->include_location.line == 5);
  assert(repeated->include_location.column == 1);
  FILE *trace_stream = tmpfile();
  assert(trace_stream != NULL);
  print_source_include_trace(trace_stream, &registry, 3);
  rewind(trace_stream);
  char mapped_trace[PATH_MAX * 3] = {0};
  assert(fread(mapped_trace, 1, sizeof(mapped_trace) - 1u, trace_stream) > 0);
  assert(strstr(mapped_trace, "numbers.lisp:3:1\n") != NULL);
  assert(strstr(mapped_trace, "main.lisp:2:16\n") != NULL);
  fclose(trace_stream);
  assert(interpreter.expression_count > 0);

  bool found_nested_location = false;
  for (uint32_t i = 0; i < interpreter.expression_count; ++i) {
    const ExpressionSourceLocation *location = get_expression_source_location(&interpreter, i);
    assert(location != NULL);
    const SourceFile *file = get_source_file(&registry, location->source_id);
    assert(file != NULL);
    if (strstr(file->path, "/sub/more.lisp") != NULL) {
      assert(location->line == 1 || location->line == 2);
      found_nested_location = true;
    }
  }
  assert(found_nested_location);
  EvalOutcome outcome = evaluate(&interpreter, NULL, NULL);
  assert(outcome.kind == EVAL_OK);
  reset_interpreter(&interpreter);
  clean_source_registry(&registry);

  // Includes do not depend on source-map support or an allocated registry.
  InterpreterStorage without_map = storage;
  without_map.expression_source_locations = NULL;
  without_map.expression_source_location_capacity = 0;
  Interpreter unmapped = {0};
  assert(configure_interpreter_storage(&unmapped, &without_map));
  assert(parse_source_with_includes(
    &unmapped,
    main_source,
    strlen(main_source),
    main_path,
    NULL,
    &error
  ));
  assert(get_expression_source_location(&unmapped, 0) == NULL);
  outcome = evaluate(&unmapped, NULL, NULL);
  assert(outcome.kind == EVAL_OK);
  reset_interpreter(&unmapped);

  // Includes in stdin input use the current working directory as their base.
  char cwd[PATH_MAX];
  assert(getcwd(cwd, sizeof(cwd)) != NULL);
  assert(chdir(directory) == 0);
  const char stdin_source[] = "(include \"shared.lisp\") (assert (== (square 2) 4))";
  assert(parse_source_with_includes(
    &unmapped,
    stdin_source,
    strlen(stdin_source),
    "/dev/stdin",
    NULL,
    &error
  ));
  outcome = evaluate(&unmapped, NULL, NULL);
  assert(outcome.kind == EVAL_OK);
  reset_interpreter(&unmapped);
  assert(chdir(cwd) == 0);

  // Missing files and malformed include forms are parsing failures.
  const char *missing = "(include \"missing.lisp\")";
  assert(!parse_source_with_includes(
    &unmapped, missing, strlen(missing), main_path, NULL, &error
  ));
  assert(strstr(error, "Unable to open include") != NULL);
  assert(strstr(error, "1:1:") != NULL);
  assert(unmapped.expression_count == 0);

  const char *dynamic = "(include (str \"shared\" \".lisp\"))";
  assert(!parse_source_with_includes(
    &unmapped, dynamic, strlen(dynamic), main_path, NULL, &error
  ));
  assert(strstr(error, "literal string path") != NULL);
  const char *extra = "(include \"shared.lisp\" 42)";
  assert(!parse_source_with_includes(
    &unmapped, extra, strlen(extra), main_path, NULL, &error
  ));
  assert(strstr(error, "exactly one") != NULL);

  // A syntax error inside an included file identifies that file.
  const char *include_bad = "(include \"bad.lisp\")";
  assert(!parse_source_with_includes(
    &unmapped, include_bad, strlen(include_bad), main_path, NULL, &error
  ));
  assert(strstr(error, "bad.lisp:2:1:") != NULL);
  assert(strstr(error, "Unterminated list") != NULL);
  assert(unmapped.expression_count == 0);

  // The diagnostic API retains the deepest error site and both include sites,
  // even without source mapping or retained included file buffers.
  const char *relay_source = "(include \"sub/relay.lisp\")\n";
  ParseDiagnostic diagnostic = {0};
  assert(!parse_source_with_includes_diagnostic(
    &unmapped,
    relay_source,
    strlen(relay_source),
    main_path,
    NULL,
    &error,
    &diagnostic
  ));
  assert(diagnostic.has_location);
  assert(strstr(diagnostic.path, "/bad.lisp") != NULL);
  assert(diagnostic.location.line == 2);
  assert(diagnostic.location.column == 1);
  assert(diagnostic.location.byte_offset == strlen("(+ 1 2\n"));
  assert(strstr(diagnostic.include_trace, "sub/relay.lisp:1:1\n") != NULL);
  assert(strstr(diagnostic.include_trace, "main.lisp:1:1\n") != NULL);
  assert(strstr(error, "Unterminated list") != NULL);

  SourceRegistry failure_registry = { .dynamic = true };
  uint32_t failure_source_id = UINT32_MAX;
  assert(register_source_file(
    &failure_registry,
    main_path,
    relay_source,
    strlen(relay_source),
    &failure_source_id
  ));
  assert(!parse_source_with_includes_diagnostic(
    &interpreter,
    relay_source,
    strlen(relay_source),
    main_path,
    &failure_registry,
    &error,
    &diagnostic
  ));
  assert(failure_registry.count == 3);
  assert(diagnostic.location.source_id == 2);
  const SourceFile *bad_file = get_source_file(&failure_registry, 2);
  assert(bad_file != NULL);
  FILE *excerpt_stream = tmpfile();
  assert(excerpt_stream != NULL);
  assert(print_source_location(
    excerpt_stream,
    bad_file->source,
    bad_file->length,
    &diagnostic.location,
    3
  ));
  fclose(excerpt_stream);
  clean_source_registry(&failure_registry);
  reset_interpreter(&interpreter);

  // The root and its hard link are the same file, even with different names.
  write_source(main_path, "(include \"alias.lisp\")");
  assert(link(main_path, alias_path) == 0);
  const char *cycle = "(include \"alias.lisp\")";
  assert(!parse_source_with_includes(
    &unmapped, cycle, strlen(cycle), main_path, NULL, &error
  ));
  assert(strstr(error, "Circular include") != NULL);
  assert(unmapped.expression_count == 0);
  assert(unlink(alias_path) == 0);

  // A symbolic link to the main file is also a circular include.
  assert(symlink("main.lisp", alias_path) == 0);
  assert(!parse_source_with_includes(
    &unmapped, cycle, strlen(cycle), main_path, NULL, &error
  ));
  assert(strstr(error, "Circular include") != NULL);
  assert(unmapped.expression_count == 0);

  // Old in-memory parser entry point has no filesystem side effects.
  assert(parse_source(&unmapped, cycle, strlen(cycle), &error));
  assert(unmapped.expression_count == 3);
  reset_interpreter(&unmapped);

  assert(unlink(alias_path) == 0);
  assert(unlink(main_path) == 0);
  assert(unlink(shared_path) == 0);
  assert(unlink(numbers_path) == 0);
  assert(unlink(nested_path) == 0);
  assert(unlink(statements_path) == 0);
  assert(unlink(empty_path) == 0);
  assert(unlink(bad_path) == 0);
  assert(unlink(relay_path) == 0);
  assert(rmdir(nested_dir) == 0);
  assert(rmdir(directory) == 0);

  puts("Parser include tests passed");
  return 0;
}
