# Lispico

A small, explicitly typed LISP-like interpreter for embedded systems and
desktops.

**WORK IN PROGRESS: Not all features are implemented yet.**

This is not a conventional LISP interpreter. It borrows LISP's S-expression
syntax primarily for its parsing simplicity and compatibility with existing
LISP syntax highlighting in code editors.

Lispico is intended for dynamically parsing and evaluating DSL-like programs
on embedded platforms such as the Raspberry Pi Pico, as part of firmware
written in C. That's also where the name comes from: LISP-like + Pico.

## Features

- **TODO** before removing WIP warning
  * Automated tests
    + Parser tests (valid/invalid syntax, comments, escaping, numeric bounds)
    + Evaluator tests for every supported operation
    + Control-flow tests (`if`, `do`, `goto`, `label`, `exit`)
    + Memory cleanup and repeated evaluation tests
    + Tests for expression, symbol, and allocation capacity limits

  * Evaluator: Handle maximum evaluation recursion depth / stack exhaustion

  * Documentation: Document execution semantics, resource limits,
    and supported language syntax

  * Evaluator: Runtime allocation for arrays (`(arr-*)`)
  * Evaluator: More informative error messages
  * Evaluator: Memory wipe and cleanup for allocated runtime entities
  * Evaluator: Implement custom expressions list cloning (and expression allocations)

  * Add more string manipulation operations (e.g. per-char reads and writes)

  * Compilation parsed expressions to binary
    + Support compiling to binary and evaluating binaries (skipping parsing step)
    + Binary format: Validate loaded binaries
      (version, sizes, expression indexes, allocation references)

  * Try to build for Raspberry Pico (using `nixpkgs.gcc-arm-embedded`)
    and make necessary changes to make it work on that platform

  * Provide examples how to use it on Raspberry Pico (making parser optional)

  * Interpreter executable:
    + Variables as inputs: `--var foo u32:123`/`--var bar string:test`
    + `--print-result` Print last evaluation result (disabled by default)
    + `--source-file` For input source file (reading from `/dev/stdin` by default)
    + `--` Raw arguments separator for arguments passing for the script only
      (`args` variable, C `argv`/`argc` are already passed to the script)

To keep the implementation simple, the language supports only a limited set of
features:

- Global state stored in a single static hash-table.

  There are no lexical name scopes. If you need some level of name scoping you
  can use name prefixes for example.

- Primitive data types

  * `u32`
  * `i32`
  * `f32` (32-bit `float` in C)

  Non-negative numbers are defaulting to `u32`, negative ones defaulting to
  `i32`, fractional numbers default to `f32`. When you want specific type
  for a number you can use `(type)` expression:

  * `(type i32 123)`
  * `(type f32 123)`

  Same `(type)` can be used for type-conversion
  (be aware of type bounds and number overflows):

  * `(type i32 (type u32 123))`
  * `(type i32 (type f32 123.321))` — Returns `123` (truncated)
  * `(type i32 (type f32 -123.321))` — Returns `-123` (truncated)

  Invalid out-of-bounds numeric literals are rejected:

  * `4294967295` → OK
  * `4294967296` (`u32` that is bigger than `UINT32_MAX`) → Failure
  * `-2147483648` → OK
  * `-2147483649` (`i32` that is smaller than `INT32_MIN`) → Failure

- Strings

  * Semantics and behavior

    + Behavior notes

      + String indexes and lengths are measured in bytes, not Unicode
        characters.
      + String modifications are performed in place without changing
        `String::length`.
      + Writing beyond the existing string boundaries causes an evaluation
        failure.
      + String data may contain `\0` bytes;
        `String::length` determines its actual length.

    + `(str-size STRING)` —
      Returns length in bytes (`u32`; no allocation)

    + `(str-char STRING INDEX)` —
      Reads a byte (`u32`) from any evaluated string (no allocation)

    + `(str-get "NAME" INDEX)` —
      Reads a byte (`u32`) from a string variable (no allocation)

    + `(str-set "NAME" INDEX BYTE)` —
      Changes one byte, numeric BYTE from 0 to 255 (no allocation)

    + `(str-set "NAME" INDEX STRING)` —
      Overwrites bytes starting at INDEX
      (does not change the string length; no allocation).
      Entire STRING must fit starting from INDEX
      (otherwise an out-of-bounds evaluation failure occurs)

    + `(str-set "NAME" STRING)` —
      Overwrites from the beginning and zero-fills the remainder with `\0`
      (does not change the string length; no allocation).
      Entire STRING must fit the original string variable length
      (otherwise an out-of-bounds evaluation failure occurs)

    + `(str-new "NAME" LENGTH)` —
      Defines a new zero-initialized string variable (allocates)

    + `(str-copy STRING)` —
      Returns an independent copy of a string (allocates).

      Useful when a string is shared by reference and may be mutated elsewhere
      (e.g. strings stored in arrays).

    + `(str STRING-1 [STRING-N…])` —
      Creates a new string with all string arguments concatenated together
      (allocates)

    + `(var "NAME" STRING)` —
      Creates an independent copy of the string and saves it as a variable
      (allocates)

    + `(set "NAME" STRING)` —
      Replaces the string contents with a new string while preserving original
      string length (zero-fills the remainder with `\0`; no allocation). The
      behavior is equivalent to `(str-set "NAME" STRING)`.

      If the provided string length exceeds the original string variable length
      an out-of-bounds evaluation failure occurs.

      While `(var "NAME" STRING)` creates an independent copy of a string
      (to avoid, for example, modifying parsed expression string literals),
      `(set "NAME" STRING)` modifies the string variable in-place without
      new allocations. This behavior is intentionally asymmetric.

  * String conversions (to/from strings)

    + Number to string

      `(type s STRING)` returns existing string value (no allocation),
      for all other cases a new string is allocated.

      + `(type s 123)` → `"123"`
      + `(type s -123)` → `"-123"`
      + `(type s 3.5)` → `"3.5"`
      + `(type s "hello")` → `"hello"`

    + String to number (the target type determines how a string is parsed)
      + `(type u32 "123")` → `TYPE_U32(123)`
      + `(type i32 "-123")` → `TYPE_I32(-123)`
      + `(type f32 "3.5")` → `TYPE_F32(3.5)`
      + `(type f32 "123")` → `TYPE_F32(123.0)`
      + `(type u32 "-123")` → Evaluation failure (`u32` cannot represent negative values)
      + `(type i32 "4294967295")` → Evaluation failure (out of range)
      + `(type u32 "12xyz")` → Evaluation failure (invalid numeric string)

- Arrays

  * `(arr-new "foo" u32 16)` where 16 is an array size and `foo` is the variable name
  * `(arr-get "foo" 0)` to get an array element by index 0
  * `(arr-set "foo" 0 123)` to set an array element by index 0 to `123` value
  * `(arr-size "foo")` to get `foo` array size (e.g. `16` of `u32` type)

- There are no user-defined functions (at least for now)

- There are no conventional loops, only `(label "foo")` definitions and
  `(goto foo)` jumps

  Note that labels are registered dynamically, during evaluation, not during
  parsing. So you can only jump backwards, not forward. This **won’t work:**

  ```lisp
  (goto foo)
  (label "foo")
  ```

  `foo` label is not defined/registered yet when `(goto)` is called.

- Supported instructions

  * Basic arithmetics

    + `(+ 2 3)` — Addition
    + `(- 3 2)` — Subtraction
      (both operands default to `u32`, so the result is also `u32`; if
      subtraction would mathematically produce a negative result, unsigned
      arithmetic wraps modulo `2^32`; use explicit `i32` operands when
      negative results are required)
    + `(* 2 3)` — Multiplication
    + `(/ 10.0 5.0)` — Floating-point division
      (attempt to divide by zero results in failure;
      be aware that `(/ 10 5)` will result into default `u32` type,
      so it would behave like `(div)` and you will lose the fractional part of
      the quotient)
    + `(div 10 4)` — Integer division
    + `(mod 5 3)` — Modulo
    + `(floor 123.321)` — Get a floor of a fractional number
    + `(ceiling 123.653)` — Get a ceiling of a fractional number
    + `(round 123.653)` — Get a rounded fractional number
    + `(trunc 123.653)` — Get a truncated fractional number
      (the result differs from `(floor)` for negative numbers)

    Note that there is no type promotion (there is no implicit type conversion).
    All these functions expect all arguments be of the same type (otherwise the
    interpreter refuses to evaluate and exits with a failure). And the resulting
    value is also of the same type. That means you have to watch the types you
    pass as inputs and watch the default types. Prefer explicit typing using
    `(type)` for numeric literals for predictable explicit behavior.

    Here are some examples of different types behavior:

    - `(/ 7.5 2.0)` — Result is `3.75` (`f32`)
    - `(div 7.5 2.0)` — Result is `3.0` (`f32`)
    - `(/ 7 2)` — Result is `3` (`u32`)
    - `(div 7 2)` — Result is `3` (`u32`)
    - `(div -7.5 2.0)` — Result is `-3.0` (`f32`)
    - `(div -7 2)` — Failure (`i32` vs. `u32` type mismatch).
      Use explicit type conversion (`(div -7 (type i32 2))`)
    - `(mod 5.5 2.0)` — Result is `1.5`
      (C `fmodf` behavior for `f32`;
      for `u32` and `i32` C `%` is used instead)
    - `(- 2 3)` — Result is `UINT32_MAX`/`4294967295`
      (defaulting to `u32`, number underflow, you might want to explicitly
      convert the values to `i32` first when expecting negative value as a
      result of the expression `(- (type i32 2) (type i32 3))` to get
      `i32` resulting type)

  * Variables
    + `(var "foo" 123)` — Define a variable (`u32` type; can be defined only once)
    + `(var "foo" (type i32 123))` — Define a variable with `i32` type
      (variable type is infered from the default value type)
    + `(set "foo" (+ foo 3))` — Set a value to an already defined variable

  * If-conditions and comparisons:

    + `(if (> 5 2) (println "bigger") (println "less"))`
      The condition expression is C-like
      (`0` is false and any other number is considered true)
    + `(if (> 5 2) (println "bigger"))`
      Omitting false-branch is allowed (nothing is returned in this case)
    + `true` is an alias for `1`
    + `false` is an alias for `0`

    + Comparison functions/operators

      Note that the result of these functions is always of `u32` type.
      The rule for the arguments is the same. All arguments must be of the same
      type. Otherwise the interpreter will refuse to evaluate and fail.

      + `(< a b)` — Less
      + `(<= a b)` — Less or equal
      + `(> a b)` — More
      + `(>= a b)` — More or equal
      + `(== a b)` — Equal
      + `(!= a b)` — Not equal

    + String comparisons

      All the comparison operators work for strings too. But note that these
      comparisons are case-sensitive, and lexicographic ordering compares bytes,
      not human-language alphabetical order.

  * Strings

    + Literals
      + A string wrapped with double quotes (`"foo"`)
        where quotation marks inside can be escaped with a backslash `\"`

    + String concatenation
      + `(str "foo" "bar")`
      + `(str "foo bar" "baz")`
      + `(str "foo" (str "bar" "baz"))`

  * Extendable standard library

    + It’s easy to pre-populate the “standard library” (the definitions/symbols
      hash-table) with custom FFI bindings.

      Examples of already defined functions:

      + Printing to stdout
        + `(println "hello there")`
        + `(print "hi")`
        + `(println (str "hello" "there"))`
      + Printing to stderr
        + `(eprintln "hello there")`
        + `(eprint "hi")`
        + `(eprintln (str "hello" "there"))`

      Note that these printing functions will print `\0` NUL bytes as-is,
      without actually terminating a string. Original string length is the
      actual string limit.

  * Early exit (by default last expression is returned from the program)

    + `(exit 0 123)` where `0` is exit status code and `123` is returned value
      (defaulting to `u32` type)

  * Expression sequences

    + `(do (+ 1 2) (+ 3 4))` — Evaluates expressions sequentially and returns
      the last expression’s result (`7` in this case).

    + Top-level program is also a sequence of expressions
      (last expression is the result of the program)

      ```lisp
      (+ 1 2)
      (+ 3 4) ; Program returns 7
      ```

- Comments

  Conventional LISP comments:

  * `; Arbitrary text until the end of a line`
  * `#| Arbitrary inline comment text |#`

- When you need to pass input values for the program you just pre-populate the
  symbols/definitions hash-table with values that can be read then inside the
  program

### Undefined behavior

Note that number overflow/underflow is preserving C behavior:

- `uint32_t` overflow/underflow — Well-defined wrapping modulo `2^32`
- `int32_t` arithmetic overflow — Undefined behavior
- `INT32_MIN / -1` — Undefined behavior (signed division overflow)
- Out-of-range `float` → `int32_t` or `uint32_t` conversion —
  Undefined behavior
- `float` overflow — Depends on the floating-point implementation;
  commonly produces infinity (`INFINITY` from `math.h`)

#### Undefined behavior exceptions

In C integer division by zero is undefined behavior, but this interpreter checks
it and fails on the attempt.

- `(/ 10 0)` / `(/ 10.0 0.0)` → Failure
- `(div 10 0)` / `(div 10.0 0.0)` → Failure
- `(mod 10 0)` / `(mod 10.0 0.0)` → Failure

## Usage examples

Enter a nix-shell (for supplying dependencies):

```sh
nix-shell
```

Build the interpreter:

```sh
./build.sh
```

Try something:

```console
$ ./lispico <<< '(println "hello")'
hello
SYM_EMPTY

$ ./lispico <<< '(println "hello")(+ 10 5)(+ (last-val) 3)'
hello
SYM_VARIABLE(TYPE_U32(18))

$ ./lispico examples/hello-world.lisp
Hello, World!
SYM_EMPTY

$ ./lispico <<'LISP'
(var "foo" 123)
(set "foo" (+ foo 3))
(type f32 foo)
LISP
SYM_VARIABLE(TYPE_F32(126.000000))
```

Or try all examples:

```sh
(set -eu; find examples/ -maxdepth 1 -type f -name '*.lisp' | while read -r f; do (set -x; ./lispico "$f") done)
```

You can also just build `lispico` derivation (or add it to your NixOS config):

```console
$ nix-build -A lispico

$ result/bin/lispico <<< '(println "hello")'
hello
SYM_EMPTY
```

## Memory management and cleanup

Lispico uses **evaluation-lifetime memory allocation**. Dynamically allocated
strings and arrays remain allocated for the duration of an evaluation, even
when they are no longer referenced by variables or expression results.

There is no garbage collection or automatic deallocation of individual runtime
values. This is intentional, to keep memory management simple and predictable,
particularly on embedded platforms.

Memory is managed in three categories:

- **Evaluation memory:** Runtime allocations, such as dynamically created strings
  and arrays. These are wiped and freed by `clean_evaluation()`.
- **Symbols:** Global variables, labels, and native function registrations.
  The symbol table is wiped by `clean_symbols()`.
- **Expressions:** Parsed expressions and their associated allocations,
  including string literals. These are wiped and freed by `clean_expressions()`.

After evaluating a program, call:

```c
clean_evaluation();
clean_symbols();
clean_expressions();
```

**Important:** The `EvalOutcome` returned by `evaluate()` may contain pointers
to memory managed by the evaluator or the parsed expressions. These pointers
become invalid after the corresponding cleanup functions are called.

If the resulting value needs to survive cleanup, make an independent copy of
its data **before** cleanup. Copying the `EvalOutcome`, `Datum`, `String`, or
`Array` structures alone is not sufficient for strings and arrays, because
their underlying data is referenced through pointers. Strings must be copied
using their explicit lengths, and arrays containing strings require copying
both the array elements and their referenced string data.

Numeric results are stored directly in the value structure and do not require
additional allocations or deep copying.

## Testing

You can run all the tests:

```console
# Enter nix-shell first (to supply dependencies)
$ nix-shell

# Build the interpreter first
$ ./build.sh

# Run all test suites sequentially
$ tests/run.sh
```

Or you can run them individually:

```console
# Enter nix-shell first (to supply dependencies)
$ nix-shell

# Build the interpreter first
$ ./build.sh

$ tests/run-examples.sh
$ tests/run-c.sh
```

## Local Vim configuration example

```vim
se cc=81,101 tw=100

augroup force_c_headers
  autocmd!
  autocmd BufReadPost,BufNewFile *.h setlocal filetype=c
augroup END

let g:ale_linters_explicit = 1
let g:ale_linters = {'c': ['cc'], 'sh': ['shellcheck']}
let g:ale_c_cc_executable = 'gcc'
let g:ale_c_cc_options = '-std=c23 -Wall -Wextra -Wpedantic -Wno-pragma-once-outside-header'
```

## License

SPDX: MIT

All files in this repository (apart from third-party dependencies) are
licensed under [MIT](LICENSE).
