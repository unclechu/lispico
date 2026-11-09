# Lispico

A small, explicitly typed Lisp-like programming language for desktops, designed
for embedding in microcontrollers, game engines, and other applications.

**WORK IN PROGRESS: Not all first-release features are implemented yet.**

This is not a conventional LISP interpreter. It borrows LISP’s S-expression
syntax primarily for its parsing simplicity and compatibility with existing
LISP syntax highlighting in code editors.

Lispico is intended for dynamically parsing and evaluating DSL-like programs
on embedded platforms such as the Raspberry Pi Pico, as part of firmware
written in C. That’s also where the name comes from: LISP-like + Pico.

See [examples](examples), they are also used as regression tests.

## Table of contents

- [Features](#features)
  * [Primitive data types](#primitive-data-types)
  * [Basic arithmetics](#basic-arithmetics)
  * [Variables](#variables)
  * [Strings](#strings)
  * [Arrays](#arrays)
  * [Control flow and sequences](#control-flow-and-sequences)
  * [Leaving sequences (`leave`)](#leaving-sequences)
  * [Deferred expressions (`defer`)](#deferred-expressions)
  * [Shebang support](#shebang-support)
  * [File I/O](#file-io)
  * [Environment variables](#environment-variables)
  * [Sleep functions](#sleep-functions)
  * [**TODO** for next releases](#todo)
- [Undefined behavior](#undefined-behavior)
- **[Usage examples](#usage-examples)**
- [Memory management and cleanup](#memory-management-and-cleanup)
- [Interpreter instances and thread safety](#interpreter-instances-and-thread-safety)
- [Testing](#testing)
- [Local Vim configuration example](#local-vim-configuration-example)
- [License](#license)

## Features

- **TODO** before removing WIP warning

  * Automated tests
    + Parser tests (valid/invalid syntax, comments, escaping, numeric bounds)
    + Evaluator tests for every supported operation
    + Control-flow tests (`if`, `do`, `goto`, `label`, `exit`)
    + Tests for expression, symbol, and allocation capacity limits
    + Memory cleanup and repeated evaluation tests
      + Evaluate multiple programs sequentially in the same process.
      + Verify cleanup after successful evaluations, failures, and exits.
      + Verify runtime allocations are wiped and released.
      + Verify parser allocations are wiped and released.
      + Verify symbols and control-flow state are reset between evaluations.
      + Verify returned strings are copied before cleanup when needed.

  * Dynamic function name (support strings as function names — `((str "print" "ln") "foo")`)

  * Using Lispico as a dynamic library option

  * Documentation: Document execution semantics, resource limits,
    and supported language syntax

  * Compilation parsed expressions to binary
    + Support compiling to binary and evaluating binaries (skipping parsing step)
    + Binary format: Validate loaded binaries
      (version, sizes, expression indexes, allocation references)

  * Raspberry Pico platform support

    * Try to build for Raspberry Pico (using `nixpkgs.gcc-arm-embedded`)
      and make necessary changes to make it work on that platform

    * Provide examples how to use it on Raspberry Pico (making parser optional)

  * Interpreter executable:
    + Variables as inputs: `--var foo u32:123`/`--var bar string:test`
    + `--print-result` Print last evaluation result (disabled by default)
    + `--source-file` For input source file (reading from `/dev/stdin` by default)
    + `--` Raw arguments separator for arguments passing for the script only
      (`args` variable, C `argv`/`argc` are already passed to the script)

  * Dynamically growing interpreter storage

    + Add optional automatic growth for interpreter tables when their
      capacities are reached.

    + Support dynamic growth of:
      + Expression table.
      + Symbol table.
      + Runtime allocation tracking table.
      + Other expandable interpreter resources.

    + Allow the host to choose between fixed-capacity and dynamically growing
      storage.

    + Support configurable initial capacities and growth strategies.

    + Possible growth strategies:
      + Fixed increments (e.g. 50 → 100 → 150 entries).
      + Multiplicative growth (e.g. 50 → 100 → 200 entries).
      + Custom growth policies provided by the host.

    + Use a configurable allocator or allocation callbacks, allowing
      platform-specific allocation strategies.

    + For dynamically allocated tables, use `realloc()` or equivalent
      allocation mechanisms when growth is required.

    + Preserve existing contents and expression indices when tables grow.

    + Ensure symbol-table growth correctly handles hash-table resizing
      and rehashing.

    + Handle allocation failures gracefully without corrupting interpreter
      state.

    + Allow maximum capacities to be configured, even when dynamic growth
      is enabled.

    + Expose initial capacities, maximum capacities, and growth policies
      through the desktop CLI where appropriate.

    + Embedded hosts may disable growth entirely to guarantee bounded
      memory consumption.

To keep the implementation simple, the language supports only a limited set of
features:

- Global state stored in a single static hash-table.

  There are no lexical name scopes. If you need some level of name scoping you
  can use name prefixes for example.

- <a id="primitive-data-types"></a>Primitive data types

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

  Numeric literals support underscores (`_`) as visual digit separators to
  improve readability of large numbers. Underscores are ignored when parsing
  the numeric value and do not affect its type.

  * `1_000_000` → `1000000` (`u32`)
  * `-1_000_000` → `-1000000` (`i32`)
  * `1_234.5_0` → `1234.5` (`f32`)
  * `1_000e+3` → `1000000.0` (`f32`)
  * `1.25e1_0` → `12500000000.0` (`f32`)

  Underscores are only permitted between two decimal digits, including in
  the integer, fractional, and exponent portions of a number.

  * Valid: `1_000`, `12_34_56`, `1_2.3_4`, `1_000e+3`, `1e1_0`
  * Invalid: `1_`, `1__000`, `1_.5`, `1._5`, `1e_3`, `1e+_3`

  The same syntax is accepted when converting strings to numbers:

  * `(type u32 "1_000_000")` → `TYPE_U32(1000000)`
  * `(type i32 "-1_000_000")` → `TYPE_I32(-1000000)`
  * `(type f32 "1_234.5")` → `TYPE_F32(1234.5)`

  Underscores do not affect numeric range validation. For example,
  `4_294_967_296` is still out of range for `u32`.

  The maximum numeric text length (`NUMBER_TEXT_CAPACITY`) includes
  underscores.

- Booleans

  There are no real booleans in the language, there is no boolean type.
  Booleans work similar to C `bool`. `0` (`u32`) means false and anything but
  `0` means true. There are `true` (`u32` `1`) and `false` (`u32` `0`) variables
  defined in the stdlib. `(if)` accepts a numeric value as a condition.

- <a id="strings"></a>Strings

  * Literals
    + A string wrapped with double quotes (`"foo"`)
      where quotation marks inside can be escaped with a backslash `\"`

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
      Returns the length of a string in bytes (`u32`; no allocation).

      + `(str-size "")` → `0`
      + `(str-size "hello")` → `5`
      + `(str-size "hello world")` → `11`
      + `(str-size (str "abc" "def"))` → `6`

      The argument can be any evaluated string expression, including string
      literals, variables, concatenations, and string conversions.

      The length is read directly from `String::length`, not calculated by
      searching for a `\0` terminator. Embedded `\0` bytes are included in
      the length.

      String lengths are measured in bytes, not Unicode characters.
      The result is always `u32`. A length exceeding the `u32` range
      causes an evaluation failure.

      Non-string arguments and incorrect argument counts cause evaluation
      failures. No memory allocation or string modification is performed.

    + `(str-len STRING)` —
      Returns the number of bytes preceding the first `\0` byte (`u32`;
      no allocation).

      + `(str-len "")` → `0`
      + `(str-len "hello")` → `5`
      + `(str-len "abc\0def")` → `3`
      + `(str-len "\0abc")` → `0`
      + `(str-len (str "abc" "\0" "def"))` → `3`

      The argument can be any evaluated string expression, including
      literals, variables, concatenations, and string conversions.

      Unlike `(str-size)`, which returns the complete `String::length`,
      `(str-len)` stops counting at the first `\0` byte.

      If no `\0` byte exists within `String::length`, the full string
      length is returned.

      The function uses a bounded search and never reads beyond
      `String::length`, even when the string is not null-terminated.

      String lengths are measured in bytes, not Unicode characters.
      The result is always `u32`. A resulting length exceeding the
      `u32` range causes an evaluation failure.

      Non-string arguments and incorrect argument counts cause evaluation
      failures. No memory allocation or string modification is performed.

    + `(str-char STRING [INDEX])` —
      Reads a byte from any evaluated string and returns it as `u32`
      (no allocation).

      + `(str-char "!")` → `33` (ASCII `!`)
      + `(str-char "A")` → `65` (ASCII `A`)
      + `(str-char "\0")` → `0` (NUL byte)
      + `(str-char "hello" 0)` → `104` (ASCII `h`)
      + `(str-char "hello" 4)` → `111` (ASCII `o`)
      + `(str-char "hello")` → Evaluation failure (index required)
      + `(str-char "")` → Evaluation failure (index required)
      + `(str-char "hello" 5)` → Evaluation failure (out of bounds)

      The first argument can be any evaluated string expression, including
      literals, variables, concatenations, and string conversions.

      `INDEX` is optional only when the string contains exactly one byte.
      When omitted, it defaults to `0`. Otherwise, an explicit index is
      required.

      When provided, `INDEX` must evaluate to `u32`. String indexes are
      zero-based and must be strictly less than `String::length`.

      Bytes are returned as unsigned values in the range `0` to `255`,
      regardless of whether C’s `char` type is signed.

      The function reads individual bytes, not Unicode characters. A UTF-8
      character encoded using multiple bytes requires an explicit index,
      even if it represents only one Unicode character.

      Embedded `\0` bytes are readable and return `0`. String boundaries
      are determined by `String::length`, not a null terminator.

      Non-string arguments, missing required indexes, invalid index types,
      and out-of-bounds indexes cause evaluation failures. The function
      does not modify strings or allocate memory.

    + `(str-get "NAME" INDEX)` —
      Reads a byte from a named string variable and returns it as `u32`
      (no allocation).

      + `(var "message" "Hello")`
      + `(str-get "message" 0)` → `72` (ASCII `H`)
      + `(str-get "message" 1)` → `101` (ASCII `e`)
      + `(str-get "message" 4)` → `111` (ASCII `o`)
      + `(str-get "message" 5)` → Evaluation failure (out of bounds)
      + `(str-get "unknown" 0)` → Evaluation failure (undefined variable)

      The first argument must evaluate to a valid symbol name as a string.
      The named symbol must exist and contain a string variable.

      The second argument must evaluate to `u32`. Indexes are zero-based
      and must be strictly less than the string variable’s byte length.

      Bytes are returned as unsigned values from `0` to `255`.
      Embedded `\0` bytes are readable and return `0`.

      String indexes are measured in bytes, not Unicode characters.
      The function does not modify the string or allocate memory.

      Unlike `(str-char STRING INDEX)`, which reads from any evaluated
      string, `(str-get)` reads from a named string variable through
      the global symbol table.

    + `(str-set)`

      + `(str-set "NAME" INDEX BYTE)` —
        Overwrites one byte of a named string variable (no allocation).

        + `(var "message" "Hello")`
        + `(str-set "message" 0 74)` → Changes `"Hello"` to `"Jello"`
        + `(str-set "message" 1 0)` → Writes a `\0` byte at index `1`
        + `(str-set "message" 0 255)` → Writes byte value `255`
        + `(str-set "message" 5 65)` → Evaluation failure (out of bounds)

        `INDEX` must be `u32` and strictly less than the string length.
        `BYTE` must be `u32` between `0` and `255`, inclusive.

      + `(str-set "NAME" INDEX STRING)` —
        Overwrites a range of bytes starting at `INDEX` (no allocation).

        + `(var "message" "Hello")`
        + `(str-set "message" 1 "abc")` → Changes `"Hello"` to `"Habco"`
        + `(str-set "message" 4 "!")` → Overwrites the final byte
        + `(str-set "message" 3 "abc")` → Evaluation failure (out of bounds)

        `INDEX` must be `u32`. The entire source string must fit within
        the remaining space starting at `INDEX`.

        Bytes outside the replaced range remain unchanged. The string length
        is preserved, and no zero-filling is performed.

        Empty replacement strings are allowed, including at an index equal
        to the destination string’s length.

      + `(str-set "NAME" STRING)` —
        Overwrites the contents of a named string variable from the beginning,
        zero-filling any remaining bytes (no allocation).

        + `(var "message" "Hello")`
        + `(str-set "message" "Hi")` → `"Hi\0\0\0"`
        + `(str-set "message" "")` → `"\0\0\0\0\0"`
        + `(str-set "message" "Goodbye")` → Evaluation failure (out of bounds)

        The replacement must not be longer than the existing string variable.
        The original `String::length` is preserved, even when the replacement
        is shorter or contains embedded `\0` bytes.

      All three forms modify the existing string variable in place and return
      the modified string value (`TYPE_STRING`).

      The first argument must evaluate to a valid name of an existing string
      variable. The source string can be any evaluated string expression.

      String indexes and lengths refer to bytes, not Unicode characters.
      Embedded `\0` bytes are treated as ordinary bytes.

      Invalid argument types, invalid variable names, undefined variables,
      and out-of-bounds writes cause evaluation failures. All bounds and type
      checks are performed before modifying the destination, so invalid writes
      do not partially modify the string.

      When source and destination data overlap, `memmove()` semantics are used
      to preserve the original source bytes during copying.

      Unlike `(var "NAME" STRING)`, which creates an independent string copy,
      `(str-set)` never allocates new memory or replaces the existing string
      buffer.

      `(set "NAME" STRING)` uses the same in-place replacement behavior as
      `(str-set "NAME" STRING)`.

    + `(override "NAME" STRING)` —
      Completely replaces an existing symbol with a new string variable,
      including its type and byte length. Like `(var)`, it allocates an
      independent NUL-terminated string buffer rather than modifying the
      previous contents in place.

    + `(str-new "NAME" LENGTH)` —
      Defines a new zero-initialized string variable with a specified initial
      byte length (allocates).

      + `(str-new "buffer" 10)` → Creates a string containing 10 `\0` bytes
      + `(str-new "empty-string" 0)` → Creates an empty string
      + `(str-new "buffer" 10)` again → Evaluation failure (already defined)
      + `(str-new "other" -1)` → Evaluation failure (length must be `u32`)

      `NAME` must evaluate to a valid, previously undefined symbol name.
      The new symbol is registered as a string variable (`TYPE_STRING`).

      `LENGTH` must evaluate to `u32` and specifies the exact number of bytes
      in the resulting string. A length of `0` is allowed.

      All bytes are initialized to `\0`. An additional `\0` byte is
      allocated after the string as a convenience terminator, but this
      byte is not included in `String::length`.

      The function allocates independent writable memory for the string.
      Its memory is tracked by the evaluator and released during
      `clean_evaluation()`.

      Subsequent `(str-set)` or `(set)` operations preserve the string length.
      Use `(str-resize)` to change the length while preserving the existing
      contents, or `(override)` to replace the string value completely.

      `(str-new)` returns the newly created string value, similarly to
      `(var)`.

      Invalid names, existing symbols, invalid argument counts or types,
      and allocation failures cause evaluation failures.

      Examples:

      ```lisp
      ; Create an empty 5-byte buffer
      (str-new "buffer" 5)

      (assert (== (str-size buffer) 5))
      (assert (== (str-get "buffer" 0) 0))

      ; Write into the buffer
      (str-set "buffer" "Hello")
      (assert (== buffer "Hello"))

      ; Shorter replacements preserve the buffer length
      (str-set "buffer" "Hi")
      (assert (== (str-size buffer) 5))
      (assert (== (str-len buffer) 2))

      ; Create a shorter independent string
      (var "short" (str-copy buffer (str-len buffer)))
      (assert (== (str-size short) 2))
      (assert (== short "Hi"))
      ```

      `(str-new)` differs from `(var "NAME" STRING)` because it allocates
      a specified amount of zero-initialized space rather than copying
      an existing string’s contents.

    + `(str-resize "NAME" LENGTH)` —
      Changes the byte length of an existing string variable while preserving
      its contents (allocates when the length changes).

      `NAME` must evaluate to the name of an existing string variable.
      `LENGTH` must evaluate to `u32`; zero is allowed.

      If the new length is smaller, only the first `LENGTH` bytes are kept.
      If it is larger, all old bytes are kept and the added bytes are filled
      with `\0`. Embedded NUL bytes are preserved as ordinary string bytes.
      The new buffer always has a trailing `\0` terminator outside its length.

      Resizing to the current length does nothing and requires no allocation.
      Otherwise, a new runtime-managed buffer is allocated and initialized
      before the variable is updated. An allocation failure leaves the original
      value and length unchanged. The previous buffer remains tracked until
      evaluation cleanup.

      `(str-resize)` returns the modified string variable, like `(str-set)`.

      Examples:

      ```lisp
      (var "message" "Hello")
      (str-resize "message" 8)
      (assert (== message "Hello\0\0\0"))
      (str-resize "message" 2)
      (assert (== message "He"))
      (str-resize "message" 0)
      (assert (== message ""))
      ```

    + String slicing

      + `(str-slice STRING FROM LENGTH)` —
        Creates an independent string containing exactly `LENGTH` bytes starting
        at zero-based byte index `FROM` (allocates).

        + `(str-slice "Hello world" 6 5)` → `"world"`
        + `(str-slice "Hello" 1 3)` → `"ell"`
        + `(str-slice "Hello" 5 0)` → `""`
        + `(str-slice "Hello" 4 2)` → Evaluation failure (out of bounds)

      + `(str-take STRING N)` —
        Creates an independent string containing exactly the first `N` bytes
        of `STRING` (allocates).

        + `(str-take "Hello" 3)` → `"Hel"`
        + `(str-take "Hello" 0)` → `""`
        + `(str-take "Hello" 6)` → Evaluation failure (out of bounds)

      + `(str-drop STRING N)` —
        Creates an independent string containing all bytes after the first
        `N` bytes of `STRING` (allocates).

        + `(str-drop "Hello" 2)` → `"llo"`
        + `(str-drop "Hello" 5)` → `""`
        + `(str-drop "Hello" 6)` → Evaluation failure (out of bounds)

      All three functions accept any evaluated string expression, including
      literals, variables, concatenations, conversions, and strings returned by
      `(arr-get)`.

      All indices and lengths must evaluate to `u32`. Bounds checking is strict:
      requests outside `String::length` cause evaluation failures rather than
      clamping or silently truncating the result.

      A zero-length slice is valid, including at the end of a string.

      All lengths and offsets are measured in bytes, not Unicode characters.
      Embedded `\0` bytes are treated as ordinary bytes and copied normally.

      Each function allocates independent runtime-managed storage for the
      resulting string, including an additional convenience `\0` terminator
      outside its `String::length`. The original string is not modified.

      Incorrect argument counts or types, invalid ranges, and allocation
      failures cause evaluation failures. The runtime allocations are released
      during `clean_evaluation()`.

    + `(str [STRING-N…])` —
      Creates a new string with all string arguments concatenated together
      (allocates)

      + Examples:
        + `(str)` → Empty string (`""`)
        + `(str "foo" "bar")` → `"foobar"`
        + `(str "foo bar" "baz")` → `"foo barbaz"`
        + `(str "foo" (str "bar" "baz"))` → `"foobarbaz"`

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

- <a id="arrays"></a>Arrays

  Lispico supports fixed-size, one-dimensional, explicitly typed arrays.
  `(arr)` creates an anonymous array value; `(arr-new)` defines a named,
  zero-initialized array variable, and `(var)` can initialize a named array
  from an existing array value. All three produce evaluated array values
  (`SYM_ARRAY`). Arrays cannot change their length or element type.

  * `(arr VALUE [VALUE…])` — Create an array value from one or more evaluated
    elements. The element type is inferred from the first argument and must be
    exactly the same for every element (`u32`, `i32`, `f32`, or `s`).
    No implicit type conversions are performed.

    + `(arr 1 2 3)` — An array of three `u32` values.
    + `(arr (type i32 1) -2)` — An array of two `i32` values.
    + `(arr "one" "two")` — An array of two strings.
    + `(arr 1 -2)` — Evaluation failure (element type mismatch).
    + `(arr)` — Evaluation failure (ambiguous element type).

    The result is an anonymous `SYM_ARRAY` value, not a named symbol. It can be
    passed directly to `(arr-get)`, `(arr-size)`, or `(arr-type)`, for example
    `(arr-get (arr 10 20 30) 1)` returns `20`.

    The elements cannot themselves be arrays. String elements are copied into
    independent runtime storage, preserving their exact lengths and embedded
    `\0` bytes. The array and its string contents are tracked until
    `clean_evaluation()`. As with other native calls, the number of arguments
    is limited by `EVAL_ARG_CAPACITY`.

  * `(var "NAME" ARRAY)` — Define a new named array, inferring its element
    type and length from an evaluated array value. This allocates independent
    writable array storage and copies all elements without changing their type.

    + `(var "numbers" (arr 10 20 30))` — Creates a named array of three `u32` values.
    + `(var "words" (arr "Hello" "World"))` — Creates a named array of two strings.
    + `(var "copy" numbers)` — Independently copies the existing `numbers` array.

    String array elements are copied into independent runtime storage,
    preserving their lengths and embedded `\0` bytes. Zero-initialized empty
    string elements (`NULL` data, zero length) remain empty without allocating
    individual string buffers. Zero-length array values can also be copied;
    the new variable retains the source element type and length of zero.

    The new array is a separate variable, so modifying its elements with
    `(arr-set)` does not affect the source array. The new name must not already
    exist. Like other array allocations, the copy remains allocated until
    `clean_evaluation()`.

  * `(set "NAME" ARRAY)` — Overwrite the elements of an existing named array
    using an evaluated array value, without changing the destination’s element
    type, length, or storage pointer. The source array must have exactly the
    same element type and must not be longer than the destination.

    + `(arr-new "numbers" u32 4)` — Creates four zero-initialized elements.
    + `(set "numbers" (arr 10 20))` — Writes `10`, `20`, `0`, `0`.
    + `(set "numbers" (arr 1 2 3 4 5))` — Evaluation failure (too many elements).
    + `(set "numbers" (arr -1))` — Evaluation failure (element type mismatch).

    Numeric array elements are copied using `memmove()`, so overlapping source
    and destination storage is handled correctly. The unused elements after
    the copied prefix are zero-filled, as if they had just been allocated by
    `(arr-new)`.

    For string arrays, every copied string receives independent runtime
    storage, preserving its length and embedded `\0` bytes. Unused trailing
    elements are reset to empty strings with `NULL` data and zero length,
    without allocating additional string buffers. Previously allocated
    string buffers remain tracked until `clean_evaluation()`.

    The function returns the destination array value (`SYM_ARRAY`). It does
    not allocate replacement array storage and preserves the destination’s
    capacity. Passing an empty array clears every destination element, while
    assigning an array to itself leaves it unchanged.

  * `(arr-new "NAME" TYPE LENGTH)` — Create a new, zero-initialized array
    with `LENGTH` elements. `TYPE` must be one of `u32`, `i32`, `f32`, or `s`.
    `LENGTH` must be `u32`, may be zero, and never changes.

  + `(arr-get ARRAY-OR-NAME INDEX)` —
    Reads an element from an array at the specified zero-based index.

    The first argument can be either an evaluated array value (`SYM_ARRAY`)
    or a string containing the name of an existing array variable.

    + `(arr-get "numbers" 0)` — Reads from the named array.
    + `(arr-get numbers 0)` — Reads from the evaluated array value.
    + `(arr-get (arr-new "numbers" u32 3) 0)` — Reads from a newly created array.
    + `(arr-set "numbers" 0 123)` → `123` (updates the named array).
    + `(arr-get numbers 0)` → `123` (reads the updated array).

    `INDEX` must evaluate to `u32` and must be smaller than the array length.

    The result is a value of the array’s element type. The operation does not
    allocate memory, modify the array, or copy its underlying storage.

    For string arrays, zero-initialized elements are returned as empty strings,
    including elements whose internal `String::data` pointer is `NULL`.

  * `(arr-set "NAME" INDEX VALUE)` — Replaces an element of a named array in
    place and returns the value stored in that element (`SYM_VARIABLE`), not
    the array (`SYM_ARRAY`).

    `INDEX` must be `u32` and strictly less than the array length. `VALUE`
    must have exactly the array’s element type (`u32`, `i32`, `f32`, or `s`);
    no implicit conversions are performed. The array’s length and element
    type remain unchanged.

    + `(arr-set "numbers" 0 123)` → `123` (`u32`)
    + `(arr-set "signed" 0 -123)` → `-123` (`i32`)
    + `(arr-set "floats" 0 1.5)` → `1.5` (`f32`)
    + `(arr-set "words" 0 "Hello")` → `"Hello"` (`s`)

    For string arrays, `(arr-set)` allocates an independent copy of the
    assigned string, including embedded `\0` bytes. Its return value
    describes the stored string, not the original source string. Replacing
    a string element doesn’t immediately release the previous buffer;
    allocations remain tracked until evaluation cleanup.

    Unlike `(arr-get)`, `(arr-size)`, and `(arr-type)`, which can accept an
    evaluated array value or a name, `(arr-set)` requires a named array.

    To read the modified array afterward, use its name:

    ```lisp
    (arr-new "numbers" u32 2)
    (assert (== (arr-set "numbers" 0 123) 123))
    (assert (== (arr-get numbers 0) 123))
    (assert (== (arr-size numbers) 2))
    ```

  * `(arr-size ARRAY-OR-NAME)` — Returns the number of elements in an array
    as `u32`.

    The argument can be either an evaluated array value (`SYM_ARRAY`) or a
    string containing the name of an existing array.

    + `(arr-size "numbers")` — Get the size of the named array.
    + `(arr-size numbers)` — Get the size of an evaluated array value.
    + `(arr-size (arr-new "buffer" u32 16))` → `16`

    Zero-length arrays return `0`.

  * `(arr-type ARRAY-OR-NAME)` — Returns the array element type identifier
    as `u32` (equals to one of: `u32`, `i32`, `f32`, or `s`).

    The argument can be either an evaluated array value (`SYM_ARRAY`) or a
    string containing the name of an existing array.

    + `(arr-type "numbers")` — Get the element type of the named array.
    + `(arr-type numbers)` — Get the element type of an evaluated array value.
    + `(arr-type (arr-new "buffer" s 16))` → `s`

    Both functions read array metadata without allocating memory, copying
    array contents, or modifying the array. Invalid arguments and incorrect
    argument counts cause evaluation failures.

  * Array slicing

    + `(arr-slice ARRAY FROM LENGTH)` — Copy `LENGTH` elements starting at the
      zero-based index `FROM` into a new array value.

      + `(arr-get (arr-slice (arr 10 20 30 40) 1 2) 0)` → `20`
      + `(arr-size (arr-slice (arr 1 2 3) 3 0))` → `0`

    * `(arr-take ARRAY N)` — Copy the first `N` elements into a new array value.

      + `(arr-get (arr-take (arr 10 20 30) 2) 1)` → `20`
      + `(arr-size (arr-take (arr 1 2 3) 0))` → `0`

    * `(arr-drop ARRAY N)` — Skip the first `N` elements and copy the remaining
      elements into a new array value.

      + `(arr-get (arr-drop (arr 10 20 30) 1) 0)` → `20`
      + `(arr-size (arr-drop (arr 1 2 3) 3))` → `0`

    All three functions require an evaluated array value (`SYM_ARRAY`) as
    their first argument. A named array can be passed by referencing its
    symbol directly, for example `(arr-take numbers 2)`. String names are not
    resolved by these functions.

    `FROM`, `LENGTH`, and `N` must be `u32`. Out-of-bounds ranges cause
    evaluation failures rather than being truncated. An empty result is valid
    when the specified range lies within the array boundaries, including
    `(arr-slice ARRAY SIZE 0)`, `(arr-take ARRAY 0)`, and
    `(arr-drop ARRAY SIZE)`.

    Results preserve the source element type, including for zero-length
    arrays. Each result has independent runtime-managed storage. Numeric
    elements are copied directly; string elements are copied independently,
    including embedded `\0` bytes. Zero-initialized empty string entries
    remain empty without allocating individual string buffers. These copies
    remain allocated until `clean_evaluation()`.

  Named array operations reject invalid names and missing or non-array
  symbols. All array operations reject incorrect argument counts or types
  and out-of-bounds indexes. No implicit conversions are performed.

  The array storage is allocated by the interpreter’s tracked runtime
  allocator. The data is released during `clean_evaluation()`; the host-owned
  allocation tracking table is not freed.

  String array elements are initially empty strings. Assigning a string copies
  its bytes, including embedded `\0` bytes, into independent runtime storage.
  Overwriting an element does not immediately free its previous string buffer,
  because existing evaluated values may still reference it. Repeated writes
  therefore consume runtime allocation entries until cleanup.

  ```lisp
  (arr-new "foo" u32 16)
  (arr-set "foo" 0 123)
  (assert (== (arr-get "foo" 0) 123))
  (assert (== (arr-size "foo") 16))
  (assert (== (arr-type "foo") u32))
  ```

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

  Labels inside `(if)` branches are also reachable after their branch has
  executed at least once. A `(goto)` to such a label enters that branch
  directly, without re-evaluating the `(if)` condition, and skips expressions
  preceding the label in any enclosing `(do)` blocks. For example, even after
  `enabled` changes to `false`, the following jump can reach `again`:

  ```lisp
  (var "enabled" true)
  (var "counter" 0)
  (if enabled
    (do
      (label "again")
      (set "counter" (+ counter 1))
      (if (== counter 2) (exit 0 counter))))
  (set "enabled" false)
  (goto again)
  ```

  Labels are not general-purpose entry points into arbitrary function
  arguments or `(if)` conditions:

  ```lisp
  (println (do (label "foo") "hello"))
  (goto foo) ; Evaluation failure: destination is not reachable
  ```

  ```lisp
  (if (do (label "foo") true) (println "hello"))
  (goto foo) ; Evaluation failure: destination is not reachable
  ```

- Supported instructions

  * <a id="basic-arithmetics"></a>Basic arithmetics

    + `(+ VALUE...)` — Add two or more numeric values
      + `(+ 1 2 3)` → `6`
      + `(+ 1 2 3 4 5)` → `15`
      + `(+ u32-max 1)` → `0` (`u32` wraparound)

    + `(- VALUE...)` — Subtract two or more numeric values from left to right
      + `(- 20 3 2)` → `15`
      + `(- 100 10 5 2)` → `83`
      + `(- 2 3)` → `4_294_967_295` (`u32` wraparound)

    + `(* VALUE...)` — Multiply two or more numeric values
      + `(* 2 3 4)` → `24`
      + `(* 1 2 3 4 5)` → `120`

    + `(/ VALUE...)` — Divide two or more numeric values from left to right
      + `(/ 100 5 2)` → `10` (`u32`)
      + `(/ 7.5 2.0 0.5)` → `7.5` (`f32`)
      + `(/ 7 2)` → `3` (`u32`, fractional part discarded)

    + `(div VALUE...)` — Divide two or more numeric values from left to right,
      truncating the result toward zero after each division
      + `(div 100 3 2)` → `16` (`u32`)
      + `(div 9.0 2.0 0.5)` → `8.0` (`f32`)
      + `(div -9.0 2.0 0.5)` → `-8.0` (`f32`)

    All five functions require at least two arguments of the same numeric
    type (`u32`, `i32`, or `f32`). The result preserves the argument type.
    There is no implicit type conversion.

    Arguments are processed from left to right, equivalent to repeatedly
    applying the binary operation to the accumulated result and the next
    argument.

    Examples:
    + `(- 20 3 2)` is equivalent to `(- (- 20 3) 2)`.
    + `(/ 100 5 2)` is equivalent to `(/ (/ 100 5) 2)`.
    + `(div 9.0 2.0 0.5)` is equivalent to `(div (div 9.0 2.0) 0.5)`.

    For `u32`, addition, subtraction, and multiplication wrap modulo `2^32`.
    For `i32`, results exceeding the signed 32-bit range cause an evaluation
    failure.

    For `u32` and `i32`, both `(/)` and `(div)` perform integer division,
    discarding the fractional part. For `f32`, `(/)` preserves the fractional
    result, while `(div)` truncates toward zero after each step.

    Division by zero in any argument causes an evaluation failure.

    `(mod A B)` remains a binary operation and accepts exactly two arguments.

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

    + `(abs VALUE)` — Absolute value
      + `(abs -42)` → `42` (`i32`)
      + `(abs 123)` → `123` (`u32`)
      + `(abs -3.5)` → `3.5` (`f32`)
      + `(abs -2147483648)` → Evaluation failure (`i32` overflow)

    + `(min)` and `(max)`

      + `(min VALUE...)` — Return the smallest of one or more numeric values
        + `(min 42)` → `42`
        + `(min 5 3 7 1 2)` → `1`
        + `(min -5 -10 -3 -20)` → `-20`
        + `(min 3.5 1.5 2.5)` → `1.5`
        + `(min)` → Evaluation failure (requires at least one argument)

      + `(max VALUE...)` — Return the largest of one or more numeric values
        + `(max 42)` → `42`
        + `(max 5 3 7 1 2)` → `7`
        + `(max -5 -10 -3 -20)` → `-3`
        + `(max 3.5 1.5 2.5)` → `3.5`
        + `(max)` → Evaluation failure (requires at least one argument)

      Both functions accept one or more `u32`, `i32`, or `f32` values. All
      arguments must have the same type, and the result preserves that type.
      No implicit numeric conversions are performed. Non-finite `f32`
      arguments cause an evaluation failure.

    + `(clamp VALUE MIN MAX)` — Restrict a value to an inclusive range
      + `(clamp 5 0 10)` → `5` (already within range)
      + `(clamp 15 0 10)` → `10` (above maximum)
      + `(clamp 0 5 10)` → `5` (below minimum)
      + `(clamp 5 10 0)` → Evaluation failure (invalid range)

    All four functions support `u32`, `i32`, and `f32` and preserve their
    argument type. Functions accepting multiple arguments require all
    arguments to have the same type, without implicit conversion.

    Non-finite `f32` inputs (NaN or infinity) cause an evaluation failure.
    `(clamp)` also fails if `MIN` exceeds `MAX`.

    + `(pow BASE EXPONENT)` — Raise a number to a power
      + `(pow 2 10)` → `1024` (`u32`)
      + `(pow -2 (type i32 3))` → `-8` (`i32`)
      + `(pow 4.0 0.5)` → `2.0` (`f32`)
      + `(pow 2.0 -1.0)` → `0.5` (`f32`)
      + `(pow 0 0)` → `1` (`u32`)

      Both arguments must have the same type (`u32`, `i32`, or `f32`).
      The result preserves that type.

      Integer exponentiation is calculated exactly, without floating-point
      conversion. Integer exponents must be non-negative. Results exceeding
      the representable range of `u32` or `i32` cause an evaluation failure
      instead of overflowing or wrapping.

      Floating-point exponentiation supports fractional and negative exponents.
      Non-finite inputs or results cause an evaluation failure, including
      invalid domains such as `(pow -2.0 0.5)`.

    + `(sqrt VALUE)` — Square root
      + `(sqrt 25.0)` → `5.0`
      + `(sqrt 2.0)` → Approximately `1.4142135`
      + `(sqrt -1.0)` → Evaluation failure

    + `(log VALUE)` — Natural logarithm (base `e`)
      + `(log 1.0)` → `0.0`
      + `(log 10.0)` → Approximately `2.302585`

    + `(log10 VALUE)` — Base-10 logarithm
      + `(log10 100.0)` → Approximately `2.0`
      + `(log10 1000.0)` → Approximately `3.0`

    + `(exp VALUE)` — Exponential function (`e` raised to `VALUE`)
      + `(exp 0.0)` → `1.0`
      + `(exp 1.0)` → Approximately `2.7182817`

    `(sqrt)`, `(log)`, `(log10)`, and `(exp)` require exactly one `f32`
    argument and return `f32`. Integer arguments are not converted
    implicitly; use `(type f32 VALUE)` when needed.

    Invalid mathematical domains, non-finite inputs, and non-finite results
    cause evaluation failures. Finite floating-point underflow to zero is
    permitted.

  * Trigonometry

    + `(sin ANGLE)` — Sine of an angle in radians
      + `(sin 0.0)` → `0.0`
      + `(sin (/ pi 2.0))` → Approximately `1.0`

    + `(cos ANGLE)` — Cosine of an angle in radians
      + `(cos 0.0)` → `1.0`
      + `(cos pi)` → Approximately `-1.0`

    + `(tan ANGLE)` — Tangent of an angle in radians
      + `(tan 0.0)` → `0.0`
      + `(tan (/ pi 4.0))` → Approximately `1.0`

    + `(asin VALUE)` — Inverse sine, returning an angle in radians
      + `(asin 0.0)` → `0.0`
      + `(asin 1.0)` → Approximately `pi / 2`
      + `(asin 1.1)` → Evaluation failure (outside `[-1.0, 1.0]`)

    + `(acos VALUE)` — Inverse cosine, returning an angle in radians
      + `(acos 1.0)` → `0.0`
      + `(acos 0.0)` → Approximately `pi / 2`
      + `(acos -1.1)` → Evaluation failure (outside `[-1.0, 1.0]`)

    + `(atan VALUE)` — Inverse tangent, returning an angle in radians
      + `(atan 0.0)` → `0.0`
      + `(atan 1.0)` → Approximately `pi / 4`

    + `(atan2 Y X)` — Angle of a vector `(X, Y)` in radians
      + `(atan2 0.0 1.0)` → `0.0`
      + `(atan2 1.0 0.0)` → Approximately `pi / 2`
      + `(atan2 -1.0 0.0)` → Approximately `-pi / 2`

      The arguments are ordered `Y`, then `X`, matching C’s `atan2f()`.
      Unlike `(atan)`, `(atan2)` uses both coordinates to determine the
      correct quadrant of the resulting angle.

    + `(hypot X Y)` — Euclidean distance from the origin to `(X, Y)`
      + `(hypot 3.0 4.0)` → `5.0`
      + `(hypot 5.0 12.0)` → `13.0`
      + `(hypot f32-max f32-max)` → Evaluation failure (overflow)

    All trigonometric functions require `f32` arguments and return `f32`.
    `(atan2)` and `(hypot)` require exactly two arguments; the other
    functions require exactly one. Integer values must be explicitly
    converted using `(type f32 VALUE)`.

    All angles are expressed in radians, not degrees. Inverse trigonometric
    functions also return radians.

    Non-finite inputs and results cause evaluation failures. `(asin)` and
    `(acos)` additionally reject inputs outside the inclusive range
    `[-1.0, 1.0]`.

    Floating-point results are approximate. In particular, using `pi` near
    a trigonometric singularity does not guarantee an exact zero, infinity,
    or evaluation failure. Actual results depend on `f32` precision and the
    platform’s mathematical library.

  * Additional mathematical and floating-point constants

    + `pi` — The mathematical constant π, approximately `3.1415927` (`f32`).
    + `e` — Euler’s number, approximately `2.7182817` (`f32`).
    + `f32-min-positive` — Smallest positive normalized `f32` (`FLT_MIN`).
    + `f32-true-min` — Smallest positive representable `f32`
      (`FLT_TRUE_MIN`), normally subnormal on IEEE 754 platforms.
    + `f32-epsilon` — Difference between `1.0f` and the next larger
      representable `f32` (`FLT_EPSILON`).

    All these constants are predefined variables of type `f32`.

    Examples:
    + `(sin (/ pi 2.0))` → Approximately `1.0`
    + `(exp 1.0)` → Approximately `e`
    + `(log e)` → Approximately `1.0`
    + `(> f32-min-positive 0.0)` → `true`
    + `(< f32-true-min f32-min-positive)` → `true` on IEEE 754 binary32
    + `(> (+ 1.0 f32-epsilon) 1.0)` → `true`

    The three floating-point boundary constants have different purposes:
    `f32-min-positive` identifies the smallest positive normal value,
    `f32-true-min` identifies the smallest positive representable value,
    and `f32-epsilon` describes the spacing between representable values
    around `1.0`.

    Subnormal arithmetic may behave differently on embedded targets that
    use flush-to-zero floating-point modes.

  * Numeric type boundaries

    + `u32-min` — Minimum `u32` value (`0`).
    + `u32-max` — Maximum `u32` value (`4_294_967_295`).
    + `i32-min` — Minimum `i32` value (`-2_147_483_648`).
    + `i32-max` — Maximum `i32` value (`2_147_483_647`).
    + `f32-min` — Most negative finite `f32` value (`-FLT_MAX`).
    + `f32-max` — Largest positive finite `f32` value (`FLT_MAX`).

    All boundary constants have their corresponding numeric types.

    For floating-point values, `f32-min` represents the lowest finite value,
    not the smallest positive value. In C, `FLT_MIN` represents the smallest
    positive normalized floating-point value.

    Examples:

    + `(== u32-max 4_294_967_295)` → `true`
    + `(== i32-min -2_147_483_648)` → `true`
    + `(< f32-min 0.0)` → `true`

  * <a id="variables"></a>Variables

    + `(var "foo" 123)` — Define a variable
      (`u32` type; can be defined only once)
    + `(var "foo" (type i32 123))` — Define a variable with `i32` type
      (variable type is infered from the default value type)
    + `(var "numbers" (arr 1 2 3))` — Define an array variable with type and
      length inferred from the array value (independent copy)
    + `(set "foo" (+ foo 3))` — Set a value to an already defined variable
    + `(set "numbers" (arr 4 5))` — Overwrite the beginning of a named array
      and clear its remaining elements without changing its capacity
    + `(override "foo" "Hi")` — Replace an existing symbol with a new value,
      allowing its type, array element type, and length to change
    + `(freeze "foo")` — Make an existing symbol immutable until the evaluation
      ends
    + `(is-frozen "foo")` — Return `1` (`u32`) if an existing symbol is frozen,
      otherwise `0` (`u32`); fail if its name is not defined

    `(freeze "NAME")` accepts the name of any existing symbol, including
    scalar variables, arrays, labels, and native functions. Calling it more
    than once for the same symbol is allowed. Frozen values remain readable,
    but `(set)`, `(override)`, `(str-set)`, `(str-resize)`, and `(arr-set)`
    fail if they attempt to modify one. `(var)` cannot redefine a symbol,
    frozen or otherwise. `(label)` may re-evaluate a frozen label only when
    the label already points to that exact expression; moving it fails.

    The user may freeze standard-library symbols as well. No symbols are
    frozen by default, and a fresh evaluation starts with unfrozen symbols.
    `(is-frozen)` works with variables, arrays, labels, and native functions
    without changing their frozen state.

    `(override "NAME" VALUE)` accepts the same scalar and array values as
    `(var)`, but requires the symbol name to exist already. It can replace any
    existing symbol, including arrays, labels, and native functions. This may
    deliberately make an existing function no longer callable.

    Strings and arrays are independently copied before the symbol is replaced,
    so self-assignment works and an allocation failure leaves the original
    symbol unchanged. Existing runtime allocations remain tracked until
    evaluation cleanup. `(override)` returns the replacement value.

    Unlike `(override)`, `(set)` preserves the destination’s existing type and
    storage size, including its fixed string length or array length.

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

    + Conjunctions and disjunctions

      + `(and (< 1 2) (> 4 3))` → equals to `true` (`u32`)
      + `(or (< 1 2) (== 4 3))` → equals to `true` (`u32`)

      + Invariants:
        + `(and true true)` → equals to `true` (`u32`)
        + `(and true true true)` → equals to `true` (`u32`)
        + `(and true false)` → equals to `false` (`u32`)
        + `(and true false true)` → equals to `false` (`u32`)
        + `(or false true)` → equals to `true` (`u32`)
        + `(or false true false)` → equals to `true` (`u32`)
        + `(or false false)` → equals to `false` (`u32`)
        + `(or false false false)` → equals to `false` (`u32`)
        + `(and true)` → equals to `true` (`u32`)
        + `(and false)` → equals to `false` (`u32`)
        + `(or true)` → equals to `true` (`u32`)
        + `(or false)` → equals to `false` (`u32`)
        + `(and)` → equals to `true` (`u32`)
        + `(or)` → equals to `false` (`u32`)

      + Truthiness rules (conditions accept different numeric types):
        + `(and 123 -1 0.5)` → equals to `true` (`u32`)
        + `(or 0 0.0 42)` → equals to `true` (`u32`)
        + `(and 123 0)` → equals to `false` (`u32`)

      + Invalid cases:
        + `(and true "hello")` → Evaluation failure (strings are not accepted)
        + `(or false "hello")` → Evaluation failure (strings are not accepted)

      + `(and)` and `(or)` are evaluator special forms with short-circuit
        evaluation. `(and)` stops at the first false condition, while `(or)`
        stops at the first true condition. Remaining expressions are not
        evaluated.

        + Short-circuit examples:

          Even though strings are not accepted in these examples they do not
          cause evaluation failure because they are not evaluated due to `(and)`
          and `(or)` short-circuiting early.

          + `(and false "hello")` equals to `false` (`u32`)
          + `(or true "hello")` equals to `true` (`u32`)

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
    + `(exit 0)` — Exit successfully with `SYM_EMPTY` as the result.
    + `(exit 1 empty)` — Exit with status `1` and an explicitly empty result.

    The exit status must be a `u32` or `i32` value in the C `int` range.
    The second argument is optional, and can be any resulting datum, including
    `empty`.

  * Empty result literal

    + `empty` evaluates to `SYM_EMPTY`, the absence of a value.
      For example, `empty` as the last program expression returns `SYM_EMPTY`.
    + `(type-of empty)` returns `void` (the type identifier), but `empty` is
      **not** a scalar `Value` of type `TYPE_VOID`.
    + `empty` is a reserved literal, not an entry in the symbol table. It
      always evaluates to `SYM_EMPTY`; `(kind-of "empty")` is `empty-kind`.
      Variables and arrays cannot be named `empty`.

  * <a id="control-flow-and-sequences"></a>Expression sequences

    + `(do (+ 1 2) (+ 3 4))` — Evaluates expressions sequentially and returns
      the last expression’s result (`7` in this case).

    + Top-level program is also a sequence of expressions
      (last expression is the result of the program)

      ```lisp
      (+ 1 2)
      (+ 3 4) ; Program returns 7
      ```

    + Each expression has a resulting value. And in a sequence there is a helper
      `(last-val)` that lets you to read that last evaluation result. So without
      nesting expressions you can manipulate the value sequentially:

      ```lisp
      (+ 1 2) ; 3
      (+ (last-val) 4) ; 7
      (* (last-val) 2) ; 14
      ```

      Compared to:

      ```lisp
      (* (+ (+ 1 2) 4) 2) ; 14
      ```

    + Sequence nesting depth

      + `(depth)` — Returns the current nesting level of `(do)` sequences as
        `u32`.

      + Top-level evaluation starts at depth `0`. Each nested `(do)` increments
        the depth by `1`.

      + Examples:
        + `(depth)` → `0`
        + `(do (depth))` → `1`
        + `(do (do (depth)))` → `2`
        + `(if true (depth))` → `0`

      + `(if)`, `(and)`, `(or)`, and ordinary native function calls do not
        increment the nesting depth themselves.

      + The depth is restored when leaving a sequence, including when evaluation
        stops due to `(goto)`, `(exit)`, or an evaluation failure. Jumping back
        into nested sequences restores the corresponding nesting depth.

  * Assertions

    + `(assert (== 10 (+ 5 5)) "Arithmetic failed")` → Success

    + `(assert (== (+ 2 2) 5) "Two plus two should equal five")` →
      Evaluation failure with error message:
      `(assert) assertion failed: Two plus two should equal five`

    + `(assert (== (+ 2 2) 5))` →
      Evaluation failure with error message: `(assert) assertion failed`

  * Type inspection

    + Primitives and strings (`(type-of VALUE)`)
      + `(type-of 123)` → equals to `u32`
      + `(type-of -123)` → equals to `i32`
      + `(type-of 123.)` → equals to `f32`
      + `(type-of true)` → equals to `u32`
      + `(type-of false)` → equals to `u32`
      + `(type-of "foo")` → equals to `s`
      + `(var "foo" 123) (type-of foo)` → equals to `u32`
      + `(arr-new "foo" u32 16) (type-of foo)` → Evaluation failure
        (dereferenced `foo` array is not a primitive or a string)
      + `(type-of (if false 123))` → equals to `void`

    + Variables (`(kind-of "NAME")`)
      + `(kind-of "unknown")` → equals to `empty-kind`
        (useful for checking if variable is defined:
        `(if (== (kind-of "unknown") empty-kind) (println "Variable “unknown” is not defined"))`)
      + `(kind-of "u32")` → equals to `var-kind`
      + `(kind-of "+")` → equals to `native-fn-kind`
        (note that evaluator special form entities are not recognized as
        variables, so `(kind-of "do")` or `(kind-of "if")` would be equal to
        `empty-kind`)
      + `(label "foo") (kind-of "foo")` → equals to `label-kind`
      + `(arr-new "foo" u32 16) (kind-of "foo")` → equals to `arr-kind`
        (you can check that a variable is an array of `u32`:
        `(assert (and (== (kind-of "foo") arr-kind) (== (arr-type "foo") u32)) "Must be an array of u32")`)

  * Debug value representation

    + `(show VALUE)` — Return a string containing the same debug representation
      used by the executable’s final-result printer, without writing to stdout.

      + `(show empty)` → `"SYM_EMPTY"`
      + `(show 123)` → `"SYM_VARIABLE(TYPE_U32(123))"`
      + `(show "hello")` → `"SYM_VARIABLE(TYPE_STRING(\"hello\"))"`
      + `(show (arr 1 2))` → `"SYM_ARRAY(ARRAY(TYPE_U32, [1, 2]))"`

      String contents are escaped using the existing `show_` renderers,
      including embedded NUL bytes. Labels are shown as `SYM_LABEL(INDEX)`,
      where `INDEX` is an internal expression index that may change when
      the source changes.

      The resulting string belongs to Lispico’s managed runtime allocations;
      it can be stored in a variable and later collected by `(gc)` when unused.

- Special forms

  Most stdlib functions live in a shared variable namespace, but there are a few
  special evaluator forms (exceptions to this rule):

  - `(do)`
  - `(if)`
  - `(and)`
  - `(or)`
  - `(leave)`
  - `(defer)`

- Comments

  Conventional LISP comments:

  * `; Arbitrary text until the end of a line`
  * `#| Arbitrary inline comment text |#`

- When you need to pass input values for the program you just pre-populate the
  symbols/definitions hash-table with values that can be read then inside the
  program

- <a id="shebang-support"></a>Shebang support

  The parser recognizes a shebang (`#!`) only at the very beginning of the
  source file. The entire first line is ignored.

  Example: `#!/usr/bin/env lispico`

  This allows Lispico programs to be executed directly as scripts when the
  source file is executable and `lispico` is available through `PATH`.

  Shebang lines count toward source line numbering for parser errors.

- <a id="file-io"></a>File I/O

  These functions are implemented in the optional desktop module
  `src/stdlib-file-io.{c,h}`. To enable them in a host application, include
  `src/stdlib-file-io.h` and call `POPULATE_STDLIB_FILE_IO()` when registering
  native functions. The desktop `interpreter.c` registers all these
  automatically.

  * `(file-exists FILENAME)` — Return a `u32` boolean indicating whether a
    filesystem path exists.

    + Returns `true` for existing files and directories, and for symbolic links
      whose targets exist. Returns `false` for missing paths, dangling symbolic
      links, and paths that cannot be accessed or resolved.
    + Uses `stat()`: this is an existence check, not a check that the path is
      a regular file or can be opened for reading or writing.

  * `(file-type FILENAME)` — Return a `u32` identifying a filesystem object,
    retaining whether the path is a symbolic link.

    + Uses `lstat()` to inspect the path itself. For symbolic links, uses
      `stat()` to classify the target. No file contents are read.
    + On the first successful call, lazily defines these `u32` variables:
      - `file-type-other` = `0`: Device, FIFO, socket, or another special object.
      - `file-type-does-not-exist` = `1`: Path does not exist.
      - `file-type-file` = `2`: Regular file.
      - `file-type-dir` = `3`: Directory.
      - `file-type-symlink-dead` = `4`: Symlink with a missing target.
      - `file-type-symlink-file` = `5`: Symlink to a regular file.
      - `file-type-symlink-dir` = `6`: Symlink to a directory.
      - `file-type-symlink-other` = `7`: Symlink to a special object.
    + Existing variables with those names are not overwritten. Like the
      read-file status constants, they are constant-ish, not immutable.
    + Missing paths (`ENOENT` or `ENOTDIR`) return `file-type-does-not-exist`.
      For symbolic links, a missing target returns `file-type-symlink-dead`.
      Permission errors, symbolic link loops, and unexpected filesystem errors
      cause evaluation failure rather than returning an inaccurate file type.

  * `(read-file FILENAME [MAX_BYTES])` — Read a file or stream and return its
    contents as a Lispico string, optionally limited to `MAX_BYTES` bytes
    (`u32`).

    + Opens the filename in binary mode, preserving arbitrary bytes (including
      embedded NULs). The final `String.length` is the exact byte count.
    + Uses the size of a regular file as an initial allocation hint. For pipes,
      `/dev/stdin`, and other streams without a usable size, it starts with a
      small buffer and grows it geometrically until EOF.
    + Without `MAX_BYTES`, the entire input is consumed, as before, and no
      status variables are created or updated.
    + With `MAX_BYTES`, the result contains at most that many bytes (excluding
      the trailing NUL terminator). Reading stops at the limit, even for pipes
      and `/dev/stdin`. A limit of `0` is valid and returns an empty string.
    + A bounded read checks one byte beyond its limit to distinguish an exact
      EOF from truncation. For `/dev/stdin`, this byte is pushed back so a later
      read can consume it. The lookahead may block if the stream is still open
      and has not supplied another byte or EOF.
    + After every **successful bounded read**, these `u32` variables exist:
      - `read-file-status-ok` = `0`: EOF reached; input fully consumed.
      - `read-file-status-truncated` = `1`: input had more bytes than the limit.
      - `read-file-status`: status of the last successful bounded read.
      They are initialized lazily on the first bounded read. A failed read
      does not update the status. Note that `read-file-status-*` variables are
      written only when they are not defined (user can override their values,
      write them before first bounded `(read-file)` call, basically free to
      break it, they won’t be restored to the correct values in this case).
    + The resulting buffer is transferred directly to the evaluator’s runtime
      allocation tracker, without copying the complete file a second time.
    + An empty file produces an empty string. Reading a stream such as
      `/dev/stdin` without a limit continues until EOF; it may block while
      waiting for input.
    + Open, read, allocation, and close errors cause evaluation failure.

  * `(write-file FILENAME CONTENT)` — Write a string to a file or stream.

    + Opens the filename in binary mode, **creating or truncating** the file.
      Existing contents are lost when opening succeeds.
    + Writes bytes in chunks of at most 4096 bytes, preserving embedded NULs
      and writing exactly `String.length` bytes.
    + Checks short writes and `fclose()` errors, including buffered output
      failures. A failure may leave a partially written file behind.
    + Supports `/dev/stdout`, `/dev/stderr`, and other writable device files.
    + Returns an empty evaluation result (`SYM_EMPTY`) on success, like the
      printing functions.

  All these functions require a nonempty, NUL-terminated string filename without
  embedded NUL bytes. File I/O is not sandboxed: filesystem permissions and
  accessible paths are determined by the host process.

  Examples:

  ```lisp
  (assert (file-exists "notes.txt"))
  (assert (== (file-type "notes.txt") file-type-file))
  (var "contents" (read-file "notes.txt"))
  (var "prefix" (read-file "notes.txt" 1024))
  (if (== read-file-status read-file-status-truncated)
    (println "Only the first 1024 bytes were read"))
  (write-file "backup.txt" contents)
  (write-file "/dev/stdout" "Hello from Lispico!\n")
  ```

  To read piped input, supply a source file to the interpreter so stdin remains
  available to the script, then use `(read-file "/dev/stdin")` inside it.

- <a id="environment-variables"></a>Environment variables

  These two functions are provided by the optional desktop module
  `src/stdlib-env-var.{c,h}`. A host application enables them by including
  `src/stdlib-env-var.h` and calling `POPULATE_STDLIB_ENV_VAR()` when
  populating native functions (as `interpreter.c` does).

  * `(env-var-has STRING)` — Return a `u32` boolean indicating whether an
    environment variable exists. A variable set to an empty string counts as
    set, so this returns `true` even when its value is `""`.

  * `(env-var-read STRING [STRING])` — Read an environment variable as a string.
    The second argument is an optional default string.

    + If the variable exists and is nonempty, return its value.
    + If it exists but is empty, return `""` when no default is provided, or
      return the default when one is provided.
    + If it does not exist, return the default when provided; otherwise fail
      evaluation.
    + This matches Bash `${NAME:-default}` behavior, without modifying the
      process environment (unlike `${NAME:=default}`).
    + The name must be a nonempty string without `=` or embedded NUL bytes.
      The default, when provided, must be a string, including when it is not
      needed. Returned strings are copied into interpreter-owned storage.

  Example:

  ```lisp
  (env-var-has "HOME")
  (var "home" (env-var-read "HOME" "/"))
  (var "editor" (env-var-read "EDITOR" "vi"))
  ```

- <a id="sleep-functions"></a>Sleep functions

  * `(sleep-s SECONDS)` — Sleep for the specified number of seconds.
  * `(sleep-ms MILLISECONDS)` — Sleep for the specified number of milliseconds.
  * `(sleep-us MICROSECONDS)` — Sleep for the specified number of microseconds.

  * Semantics and behavior

    + All three functions accept `u32`, `i32`, and `f32` numeric values.

    + Fractional values are supported and converted into the corresponding
      duration.

      Examples:
      + `(sleep-s 0.5)` → Sleep for approximately 500 milliseconds
      + `(sleep-ms 1.5)` → Sleep for approximately 1500 microseconds
      + `(sleep-us 1.5)` → Sleep for approximately 1500 nanoseconds

    + Zero durations are allowed and do not cause an intentional delay.

    + Negative values, non-finite values, and durations exceeding the supported
      range cause an evaluation failure.

    + The maximum supported duration is `UINT32_MAX` seconds.

    + Sleep functions forward the last successfully evaluated value without
      replacing it with the sleep duration or an empty result. If no previous
      nonempty evaluation result is available, they return `SYM_EMPTY`.

      For example, `123 (sleep-ms 0.5)` returns `123` as its final value.

    + Sleep functions suspend the executing thread. Actual elapsed time may be
      longer than requested due to operating system scheduling and timer
      resolution.

    + The initial implementation uses POSIX `nanosleep()` on desktop systems
      and resumes sleeping when interrupted by signals.

      **TODO** Raspberry Pi Pico support will be implemented separately.

* <a id="leaving-sequences"></a>Leaving sequences early

  + `(leave [COUNT])` — Immediately terminates one or more active `(do)`
    sequences.

  + `(leave)` is an evaluator special form, not an ordinary native function.

  + `COUNT` is an optional evaluated expression that must produce a `u32` value.

    + When omitted, `COUNT` defaults to `1`, terminating the nearest `(do)`.
    + A positive `COUNT` terminates exactly that many enclosing `(do)`
      sequences.
    + `COUNT` greater than `(depth)` causes an evaluation failure.
    + `COUNT` equal to `0` causes an evaluation failure.

  + `(leave)` requires at least one active `(do)` sequence. Calling it at the
    top level causes an evaluation failure.

  + Expressions following `(leave)` in any terminated sequence are skipped.
    Evaluation resumes after the outermost terminated `(do)` expression.

  + `(leave)` preserves the last successfully evaluated value. If there is no
    previous nonempty value, the resulting value is `SYM_EMPTY`.

  + `(leave)` can be used inside `(if)`, `(and)`, `(or)`, and native function
    arguments. It interrupts the current expression and propagates through
    the requested number of enclosing `(do)` sequences.

  + `(depth)` is restored as each sequence is terminated.

  + Examples:

    ```lisp
    ; Leave only the nearest sequence
    (do
      (leave)
      (println "Never printed"))

    ; Leave two nested sequences
    (do
      (do
        (leave 2)
        (println "Never printed"))
      (println "Also never printed"))

    (println "Evaluation continues here")

    ; Leave all active sequences
    (do
      (do
        (do
          (leave (depth))
          (println "Never printed"))))
    ```

  + `(leave)` differs from `(goto)` and `(exit)`:
    + `(leave)` terminates the requested number of enclosing `(do)` sequences.
    + `(goto)` transfers evaluation to a registered label.
    + `(exit)` terminates evaluation of the entire program.

* <a id="deferred-expressions"></a>Deferred expressions

  + `(defer EXPR)` — Registers an expression to be evaluated when the nearest
    enclosing `(do)` sequence terminates.

  + `(defer)` is an evaluator special form, not an ordinary native function.

  + `(defer)` takes exactly one expression. The expression is not evaluated
    during registration; it is evaluated when its enclosing `(do)` terminates.

  + Calling `(defer)` outside an active `(do)` sequence causes an evaluation
    failure.

  + Semantics and behavior

    + Deferred expressions belong to the particular active `(do)` sequence in
      which they were registered.

      Example: `(do (defer (println "Outer")) (do (defer (println "Inner"))))`
      prints `Inner`, then `Outer`.

    + Multiple deferred expressions execute in reverse registration order
      (last-in, first-out; LIFO).

      Example: `(do (defer (println "First")) (defer (println "Second")))`
      prints `Second`, then `First`.

    + Deferred expressions execute when their sequence terminates:
      + After normal completion of the sequence.

        Example: `(do (defer (println "Cleanup")) 123)` prints `Cleanup`
        after evaluating `123`.

      + When `(leave)` terminates the sequence.

        Example: `(do (defer (println "Cleanup")) (leave))` prints `Cleanup`.

      + When `(goto)` transfers evaluation outside the sequence.

        Example:

        ```lisp
        (var "counter" 0)
        (label "outside")
        (if (== counter 0)
          (do
            (defer (println "Cleanup"))
            (set "counter" 1)
            (goto outside)))
        ```

        Prints `Cleanup` before jumping to `outside`.

      + When `(exit)` terminates program evaluation.

        Example: `(do (defer (println "Cleanup")) (exit 0))` prints `Cleanup`
        before exiting.

      + When an evaluation failure causes the sequence to terminate.

        Example: `(do (defer (println "Cleanup")) (assert false))` prints
        `Cleanup` before reporting the evaluation failure.

    + A `(goto)` does not trigger deferred expressions unless it causes their
      enclosing `(do)` sequence to terminate. Jumps within the same active
      sequence preserve its registered deferred expressions until the sequence
      eventually exits.

      Example:

      ```lisp
      (var "counter" 0)
      (do
        (defer (println "Cleanup"))
        (label "again")
        (set "counter" (+ counter 1))
        (if (< counter 3) (goto again)))
      ```

      Prints `Cleanup` only once, after the third iteration.

    + When multiple nested sequences terminate, deferred expressions execute
      from the innermost sequence outward. Within each sequence, they execute
      in LIFO order.

      Example:

      ```lisp
      (do
        (defer (println "Outer 1"))
        (defer (println "Outer 2"))
        (do
          (defer (println "Inner 1"))
          (defer (println "Inner 2"))))
      ```

      Prints `Inner 2`, `Inner 1`, `Outer 2`, `Outer 1`.

    + Deferred expressions use the values of variables at execution time,
      not at registration time. Variables are not captured or copied.

      Example:

      ```lisp
      (var "counter" 1)
      (do
        (defer (println counter))
        (set "counter" 2))
      ```

      Prints `2`, not `1`.

    + `(defer)` registration preserves the last successfully evaluated value.
      If no previous nonempty value is available, it returns `SYM_EMPTY`.

      Example: `(do 123 (defer (println "Cleanup")))` returns `123`, while
      `(do (defer (println "Cleanup")))` returns `SYM_EMPTY` if there was no
      previous nonempty value.

    + The resulting values of deferred expressions are discarded. They do not
      change the sequence’s returned value, the forwarded value of `(leave)`,
      or the result and exit code of `(exit)`.

      Examples:
      + `(do 123 (defer 456))` returns `123`.
      + `(do 123 (defer 456) (leave))` returns `123`.
      + `(do (defer 456) (exit 0 123))` exits with code `0` and result `123`.

    + Deferred expressions execute while their enclosing `(do)` sequence is
      still active, before its `(depth)` decreases.

      Example:

      ```lisp
      (do
        (defer (println (depth))))
      (println (depth))
      ```

      Prints `1`, then `0`.

    + All registered deferred expressions are attempted, even if one fails.
      If evaluation had already failed, the original failure is preserved.
      Otherwise, the first deferred failure causes evaluation to fail.

      Example:

      ```lisp
      (do
        (defer (println "Still executed"))
        (defer (assert false "Deferred failure")))
      ```

      `Still executed` is printed even though the other deferred expression
      fails.

      If the body had already failed, for example:

      ```lisp
      (do
        (defer (assert false "Deferred failure"))
        (assert false "Original failure"))
      ```

      The original failure is preserved.

    + Deferred expressions must not transfer control outside themselves using
      `(leave)`, `(goto)`, or `(exit)`. Such attempts cause an evaluation
      failure. Control flow contained within a nested `(do)` created by the
      deferred expression is allowed.

      Example of invalid control flow:

      ```lisp
      (do
        (defer (leave)))
      ```

      Example of valid nested control flow:

      ```lisp
      (do
        (defer
          (do
            (println "Before leave")
            (leave)
            (println "Never printed"))))
      ```

      Only `Before leave` is printed.

    + A deferred expression may use nested `(do)` sequences and register
      deferred expressions within those sequences, but it cannot register
      another defer in the enclosing sequence that is already terminating.

      Example of valid nested registration:

      ```lisp
      (do
        (defer
          (do
            (defer (println "Nested cleanup"))
            (println "Nested body"))))
      ```

      Prints `Nested body`, then `Nested cleanup`.

      Example of invalid registration:

      ```lisp
      (do
        (defer
          (defer (println "Not allowed"))))
      ```

    + Re-entering a previously completed `(do)` through `(goto)` creates a new
      set of registrations for that sequence.

      Example:

      ```lisp
      (var "counter" 0)

      (do
        (label "again")
        (defer (println "Cleanup"))
        (set "counter" (+ counter 1)))

      (if (< counter 2) (goto again))
      ```

      Prints `Cleanup` twice, once for each activation of the `(do)` sequence.

    + Deferred registrations are created only when `(defer)` is actually
      evaluated. Jumping past a `(defer)` expression does not register it.

      Example:

      ```lisp
      (var "counter" 0)

      (do
        (defer (println "Cleanup"))
        (label "again")
        (set "counter" (+ counter 1)))

      (if (< counter 2) (goto again))
      ```

      Prints `Cleanup` only once. The second activation begins at the label
      after `(defer)`, so no new registration is created.

    + A `(goto)` within an active `(do)` preserves its existing deferred
      registrations, even if the jump skips the original `(defer)` expression.

      Example:

      ```lisp
      (var "counter" 0)

      (do
        (defer (println "Cleanup"))
        (label "again")
        (set "counter" (+ counter 1))
        (if (< counter 3) (goto again)))
      ```

      Prints `Cleanup` only once. The original registration survives both
      jumps because the sequence never terminates between iterations.

    + Re-entering a completed `(do)` through `(goto)` starts a new activation
      without the previous activation’s deferred registrations. Only `(defer)`
      expressions reached during the new activation are registered.

      Example:

      ```lisp
      (var "counter" 0)

      (do
        (defer (println "Before label"))
        (label "again")
        (defer (println "After label"))
        (set "counter" (+ counter 1)))

      (if (< counter 2) (goto again))
      ```

      Prints:
      + First activation: `After label`, `Before label`.
      + Second activation: `After label`.

      The second activation starts with an empty set of registrations and
      skips the first `(defer)` because it appears before the label.

    + The evaluator maintains a fixed-capacity stack of deferred expression
      indices (`EVAL_DEFER_CAPACITY`). Exceeding this capacity causes an
      evaluation failure. Registrations themselves do not allocate memory.

      Example:

      ```lisp
      (do
        (label "again")
        (defer 123)
        (goto again))
      ```

      Each iteration registers another deferred expression without leaving
      the active sequence. Eventually, the registration capacity is reached,
      causing an evaluation failure.

  + Examples:

    ```lisp
    ; Deferred expressions execute in reverse order
    (do
      (defer (println "First"))
      (defer (println "Second"))
      (println "Body"))

    ; Output:
    ; Body
    ; Second
    ; First

    ; Deferred expressions also execute on (leave)
    (do
      (defer (println "Cleanup"))
      (leave)
      (println "Never printed"))

    ; Output:
    ; Cleanup

    ; All exited sequences run their own defers
    (do
      (defer (println "Outer cleanup"))

      (do
        (defer (println "Inner cleanup"))
        (leave (depth))))

    ; Output:
    ; Inner cleanup
    ; Outer cleanup
    ```

- <a id="todo"></a>**TODO** for next releases

  * Pseudo-scoped variables vs. global ones
  * Macroses/functions
  * Sub-process invocation stdlib functions (spawning, capturing)
  * File reading & writing stdlib functions
  * Streamed parsing + evaluation
  * REPL
  * Linked lists (maybe cons-lists?)
  * Untyped arrays (including nested arrays)

  * Evaluator:
    + More informative error messages
    + Handle maximum evaluation recursion depth / stack exhaustion

  * Resumable evaluation and coroutines

    + Add a `(yield [VALUE])` special form that temporarily suspends program
      evaluation and returns control to the host.

    + `(yield)` suspends evaluation with `SYM_EMPTY`.
    + `(yield VALUE)` evaluates `VALUE` and suspends evaluation with its result.

    + The evaluator returns a new outcome kind, `EVAL_YIELD`, rather than
      `EVAL_OK` or `EVAL_EXIT`.

    + The host can inspect the yielded value, modify interpreter state
      (including variables), and resume evaluation from the suspension point.

    + Evaluation resumes after the `(yield)` expression without re-executing
      previously completed expressions.

    + Preserve active `(do)` sequences, `(depth)`, pending `(defer)`
      registrations, and other evaluation state across suspension and resumption.

    + Support repeated yielding and resumption, allowing bidirectional
      communication between Lispico and its host application.

    + Examples:

      ```lisp
      (var "input" 0)

      (yield "Waiting for input")

      ; The host modifies `input` before resuming evaluation.
      (println input)

      (yield input)
      ```

  * Evaluation inspection and external interruption

    + Allow the host to register an optional callback invoked during expression
      evaluation.

    + The callback can inspect the current expression, evaluation depth,
      symbols, variables, and other interpreter state.

    + The callback can request suspension, causing the evaluator to return
      a new outcome kind, `EVAL_INTERRUPTED`.

    + The host can inspect or modify interpreter state before resuming
      evaluation.

    + Evaluation resumes from the interrupted position without losing
      active sequences, deferred expressions, or pending control flow.

    + Define whether callbacks execute before or after expression evaluation,
      and ensure that resumption does not accidentally evaluate an expression
      twice.

    + Potential applications:
      + Debuggers and single-step execution.
      + Breakpoints and expression tracing.
      + Interactive host-controlled execution.
      + External event handling and cooperative scheduling.

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

### Host-provided interpreter storage

Lispico uses host-provided memory for its fixed-capacity interpreter tables.
The parser and evaluator do not allocate these tables themselves.

The host application provides:

* Expression storage and its capacity.
* Expression allocation tracking storage and its capacity.
* Symbol hash-table storage and its capacity.
* Runtime allocation tracking storage and its capacity.
* Deferred-expression index storage and its capacity.

Storage is configured using `configure_interpreter_storage()` before parsing
or evaluation.

The host may use static, stack, or dynamically allocated memory. Each table
has an independently configurable capacity.

The symbol table uses open addressing and requires a power-of-two capacity.

The interpreter tracks used entries independently of the configured
capacities. Exceeding a table’s capacity causes the corresponding parsing,
registration, or evaluation operation to fail.

#### Ownership and cleanup

* The host owns the table buffers and is responsible for their lifetime.
* Lispico never frees or reallocates host-provided table storage.
* `clean_expressions()`, `clean_symbols()`, and `clean_evaluation()` reset or
  wipe the corresponding tables without releasing their backing storage.
* Memory allocated by Lispico for expression data and runtime string values
  remains owned by Lispico and is wiped and freed during the corresponding
  cleanup operation.
* The host must keep all configured table buffers valid for the interpreter’s
  lifetime.
* Host-provided table buffers must not overlap.
* Configuration currently occurs once, before interpreter initialization.
* Returned string values may reference interpreter-managed memory. The host
  must copy them before cleanup if their contents are needed afterward.

The desktop executable uses statically allocated tables with the default
capacities defined in `constants.h`. Embedded applications may provide
smaller buffers or use a different memory layout without modifying the parser
or evaluator.

Automatic table growth is not yet supported. It is a separate planned feature.

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

Or try all examples (note that test environment variables required for some
examples):

```sh
(set -eu; . tests/examples.env; find examples/ -maxdepth 1 -type f -name '*.lisp' | while read -r f; do (set -x; ./lispico "$f") done)
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
strings and arrays are tracked by the evaluator and remain allocated until
evaluation cleanup, unless explicitly reclaimed with `(gc)`.

There is no automatic garbage collection. Runtime values may be reclaimed
manually, keeping memory management predictable on embedded platforms.

### Manual garbage collection

- `(gc)` — Manually reclaim unreachable runtime allocations and return a `u32`
  count of freed `EvalRuntimeAllocation` entries. Accepts no arguments.

The collector uses a simple **mark-and-sweep** algorithm:

1. Mark all runtime allocations reachable from live values.
2. Traverse the runtime allocation table and wipe and free unmarked allocations.
3. Replace each freed entry with the last entry in the table, keeping the table
   packed without shifting the remaining entries.
4. Clear the marks on surviving allocations during sweeping, preparing them
   for the next `(gc)` call.

The following values are considered GC roots:

- **Symbols:** All currently defined scalar and array variables in the
  interpreter’s symbol table.
- **Evaluation result:** The most recent value stored in
  `Interpreter::eval_outcome.result`.
- **Temporary evaluator values:** Values registered through active
  `EvalGCRootFrame` instances, including native-function arguments, sequence
  results, and values preserved during deferred evaluation.

For scalar strings, the collector marks their runtime-allocated data buffers.

For arrays, the collector marks the array’s backing storage. String arrays
additionally require marking each string element’s independently allocated
data buffer. Zero-initialized string elements with NULL data pointers do not
reference any runtime allocations.

The return value counts **freed runtime allocations**, not Lispico variables
or array elements. For example, a string array containing two independently
allocated strings may occupy three runtime allocations: one for the array
storage and two for the string buffers.

A GC call does not modify existing symbols, resize variables, or free parser
allocations. Only unreachable runtime allocations are reclaimed.

Example:

```lisp
(var "message" "Hello")
(override "message" "Hi")

(assert (== (gc) 1)) ; The previous string buffer is no longer referenced
(assert (== message "Hi"))
(assert (== (gc) 0)) ; The remaining allocations are still reachable
```

Since the evaluator retains its most recent evaluation result, an otherwise
unused temporary value may survive one collection and become collectible on
a subsequent call.

Calling `(gc)` is optional. Programs that never invoke it retain their runtime
allocations until evaluation cleanup.

The collector does not automatically discover arbitrary pointers held by host
applications or custom native functions. Such pointers do not count as GC roots
unless their values are also reachable through Lispico symbols or explicitly
registered evaluator root frames.

Host applications and custom native functions must therefore avoid retaining
unprotected runtime pointers across `(gc)` calls.

### Evaluation cleanup

Memory is managed in three categories:

- **Evaluation memory:** Runtime allocations, such as dynamically created
  strings and arrays. These are tracked by the evaluator and can be reclaimed
  with `(gc)` when unreachable. All remaining runtime allocations are wiped
  and freed by `clean_evaluation()`.

  Native modules can register a previously allocated buffer with
  `adopt_runtime_data(interpreter, data, allocated_size)`; ownership passes
  to the evaluator only when it returns `true`. Adopted allocations participate
  in garbage collection just like allocations created by
  `allocate_runtime_data()`.

- **Symbols:** Global variables, labels, and native function registrations.
  The symbol table is wiped by `clean_symbols()`.

- **Expressions:** Parsed expressions and their associated allocations,
  including string literals. These are wiped and freed by
  `clean_expressions()`. Expression allocations are not managed by `(gc)`.

After evaluating a program, call:

```c
clean_evaluation(interpreter);
clean_symbols(interpreter);
clean_expressions(interpreter);
```

Manual garbage collection does not replace these cleanup operations.
In particular, `(gc)` does not free reachable allocations or reset the symbol
table and parsed expressions.

**Important:** The `EvalOutcome` returned by `evaluate()` may contain pointers
to memory managed by the evaluator or the parsed expressions. These pointers
become invalid after the corresponding cleanup functions are called.

If the resulting value needs to survive cleanup, make an independent copy of
its data **before** cleanup. Copying the `EvalOutcome`, `Datum`, `String`, or
`Array` structures alone is not sufficient for strings and arrays, because
their underlying data is referenced through pointers.

Strings must be copied using their explicit lengths, and arrays containing
strings require copying both the array elements and their referenced string
data.

Numeric results are stored directly in the value structure and do not require
additional allocations or deep copying.

### Interpreter instances and thread safety

Lispico supports multiple independent interpreter instances through the
`Interpreter` structure. Each instance maintains its own parser, evaluator,
symbol table, runtime allocations, and control-flow state.

Interpreter instances can be used concurrently by separate threads, subject
to the following requirements:

* Each instance must have its own non-overlapping writable storage buffers.
* The host must keep those buffers valid for the lifetime of the instance.
* Parsing, evaluation, symbol modification, and cleanup of the **same**
  interpreter instance must not happen concurrently.
* Different interpreter instances do not require a shared interpreter-level
  mutex.
* Host-provided callbacks and native functions must synchronize access to
  any shared mutable application state.
* Shared external resources, such as standard output, may require additional
  synchronization if interleaved operations are undesirable.
* The host must not change process-wide settings, such as the C locale,
  concurrently with interpreter operations that depend on them.

Interpreter-owned runtime allocations are tracked and cleaned up separately
for each instance. Cleaning up one interpreter does not release another
interpreter’s storage or allocations.

#### Concurrency testing

Two C tests cover interpreter isolation:

* `tests/c/interpreter-isolation.c` verifies independent state and cleanup
  across multiple interpreter instances.
* `tests/c/interpreter-multi-thread.c` exercises concurrent parsing,
  evaluation, allocation, control flow, failures, and cleanup using eight
  pthreads and 3,000 iterations per thread.

The multithreaded test is built with GCC ThreadSanitizer using
`-fsanitize=thread` to detect data races in exercised code paths.

Passing these tests provides evidence of thread safety but does not guarantee
the absence of all concurrency bugs. The host remains responsible for
synchronization of shared resources and for ensuring that each interpreter
instance is accessed safely.

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
$ tests/run-file-io.sh
$ tests/run-c.sh
```

Measure time and memory usage for all examples individually:

```sh
nix-shell --run './build.sh && tests/measure-examples.sh'
```

## Local Vim configuration example

```vim
se cc=81,101 tw=100

" Prevent *.h files from being detected as C++
augroup force_c_headers
  autocmd!
  autocmd BufReadPost,BufNewFile *.h setlocal filetype=c
augroup END

" I prefer different treesitter syntax highlighting over default commonlisp one
augroup force_custom_lisp_syntax_hl
  autocmd!
  autocmd BufReadPost,BufNewFile *.lisp setlocal filetype=scheme
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
