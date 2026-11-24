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
  * [Pseudo-local scopes](#pseudo-local-scopes)
  * [User-defined pseudo-functions](#user-defined-pseudo-functions)
  * [Strings](#strings)
  * [Arrays](#arrays)
  * [Control flow and sequences](#control-flow-and-sequences)
  * [Leaving sequences (`leave`)](#leaving-sequences)
  * [Deferred expressions (`defer`)](#deferred-expressions)
  * [Shebang support](#shebang-support)
  * [Parse-time includes](#parse-time-includes)
  * [File I/O](#file-io)
  * [Environment variables](#environment-variables)
  * [Sleep functions](#sleep-functions)
  * [**TODO** for next releases](#todo)
- [Undefined behavior](#undefined-behavior)
- [Command-line interface](#command-line-interface)
- **[Usage examples](#usage-examples)**
- [Memory management and cleanup](#memory-management-and-cleanup)
- [Interpreter instances and thread safety](#interpreter-instances-and-thread-safety)
- [Testing](#testing)
- [Local Vim configuration example](#local-vim-configuration-example)
- [License](#license)

## Features

- **TODO** before removing WIP warning

  * Automated tests

    + Parser
      + [x] Numeric parser tests
      + [x] CLI parser tests
      + [ ] Lisp source parser syntax, comments, escaping and AST tests
      + [ ] Parser failure atomicity and limits

    + Evaluator and control flow
      + [x] Existing success and failure regression examples
      + [ ] Audit registered operations for missing cases
      + [ ] Expand combined control-flow edge cases

    + Storage and lifecycle
      + [x] Basic storage capacity tests
      + [x] Interpreter isolation and multithreading tests
      + [ ] Exact capacity boundaries and symbol table exhaustion
      + [ ] Dedicated sequential evaluation and cleanup tests
      + [ ] Returned-value lifetime and ownership tests

    + Memory verification
      + [ ] Directly verify allocation wiping and freeing
      + [ ] Run sanitizer and leak-checking builds

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

  `(do)` provides pseudo-local name scopes using generated symbol prefixes.
  The symbols themselves still live in the same global table, rather than
  introducing separate lexical variable storage.

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

    + `(var "numbers" (arr 10 20 30))` —
      Creates a named array of three `u32` values.

    + `(var "words" (arr "Hello" "World"))` —
      Creates a named array of two strings.

    + `(var "numbers-copy" numbers)` —
      Independently copies the existing `numbers` array.

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

  * `(arr-concat ARRAY [ARRAY…])` — Concatenate one or more evaluated arrays
    into a new, independent array value.

    + `(arr-concat (arr 1 2 3) (arr 4 5 6) (arr 7 8 9))` → `(arr 1 2 3 4 5 6 7 8 9)`
    + `(arr-concat (arr 1 2 3))` → `(arr 1 2 3)`
    + `(var "foo" (arr 1 2 3))`
    + `(arr-concat foo foo)` → `(arr 1 2 3 1 2 3)`

    Every argument must be an evaluated array (`SYM_ARRAY`). All arrays must
    have the same element type (`u32`, `i32`, `f32`, or `s`), including empty
    arrays. No implicit conversions are performed. At least one array is
    required; the number of arguments is limited by `EVAL_ARG_CAPACITY`.

    Empty input arrays are allowed, and concatenating only empty arrays returns
    a zero-length array retaining their common element type. The resulting
    length must fit in `u32`, and the allocation size must fit in `size_t`.

    The result has independent runtime-managed array storage. Numeric elements
    are copied directly. String elements are copied into independent buffers,
    preserving embedded `\0` bytes. Zero-initialized empty string entries
    remain empty without additional string allocations. The original arrays
    are not modified, including when the same array is supplied more than once.

    `(arr-concat)` does not automatically collect garbage. Its allocations
    remain tracked until `clean_evaluation()` or an explicit `(gc)` reclaims
    unreachable data.

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

- There are no conventional loops, only `(label "foo")` or
  `(local-label "foo")` definitions and `(goto foo)` jumps

  Note that labels are registered dynamically, during evaluation, not during
  parsing. So you can only jump backwards, not forward. This **won’t work:**

  ```lisp
  (goto foo)
  (label "foo")
  ```

  `foo` label is not defined/registered yet when `(goto)` is called.

  `(label "NAME")` defines a global label. `(local-label "NAME")` requires
  an active `(do)` scope and defines a label under `_LOCAL_N_NAME`, where `N`
  is that scope’s expression index. `(goto)` resolves labels through active
  scopes from innermost to outermost, then falls back to global labels.
  Both kinds of labels are registered during evaluation.

  For example, a loop can use a local label without interfering with a
  global label of the same name:

  ```lisp
  (var "counter" 0)
  (do
    (local-label "loop")
    (set "counter" (+ counter 1))
    (if (< counter 3) (goto loop)))
  (assert (== counter 3))
  ```

  Local labels remain ordinary `SYM_LABEL` symbols in the global symbol table.
  They follow `(label)` rules when redefined or frozen. On scope exit, they
  are reset to `TYPE_VOID` and unfrozen, like other locals, without deleting
  their entries. Re-entering the same scope can define them again, and
  recursive activations preserve and restore earlier label states. A label
  does not identify a particular function activation; ordinary `(goto)`
  reachability rules still apply.

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

    + `(+ VALUE…)` — Add two or more numeric values
      + `(+ 1 2 3)` → `6`
      + `(+ 1 2 3 4 5)` → `15`
      + `(+ u32-max 1)` → `0` (`u32` wraparound)

    + `(- VALUE…)` — Subtract two or more numeric values from left to right
      + `(- 20 3 2)` → `15`
      + `(- 100 10 5 2)` → `83`
      + `(- 2 3)` → `4_294_967_295` (`u32` wraparound)

    + `(* VALUE…)` — Multiply two or more numeric values
      + `(* 2 3 4)` → `24`
      + `(* 1 2 3 4 5)` → `120`

    + `(/ VALUE…)` — Divide two or more numeric values from left to right
      + `(/ 100 5 2)` → `10` (`u32`)
      + `(/ 7.5 2.0 0.5)` → `7.5` (`f32`)
      + `(/ 7 2)` → `3` (`u32`, fractional part discarded)

    + `(div VALUE…)` — Divide two or more numeric values from left to right,
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

      + `(min VALUE…)` — Return the smallest of one or more numeric values
        + `(min 42)` → `42`
        + `(min 5 3 7 1 2)` → `1`
        + `(min -5 -10 -3 -20)` → `-20`
        + `(min 3.5 1.5 2.5)` → `1.5`
        + `(min)` → Evaluation failure (requires at least one argument)

      + `(max VALUE…)` — Return the largest of one or more numeric values
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
    + `(get "foo")` — Read a variable by its name, evaluated at runtime
    + `(var-ref "foo")` — Return a variable’s resolved stored name as a string,
      suitable for passing to name-based operations in other functions
    + `(variables)` — Return all defined symbols’ stored names
    + `(local-variables)` —
      Return visible symbols’ stored names without masked entries
    + `(get (str "fo" "o"))` — Resolve a variable name dynamically
    + `(reset "foo")` — Reset an existing scalar or array variable to `TYPE_VOID`
    + `(set "foo" (+ foo 3))` — Set a value to an already defined variable
    + `(set "numbers" (arr 4 5))` — Overwrite the beginning of a named array
      and clear its remaining elements without changing its capacity
    + `(override "foo" "Hi")` — Replace an existing symbol with a new value,
      allowing its type, array element type, and length to change
    + `(freeze "foo")` — Make an existing symbol immutable until the evaluation
      ends
    + `(is-frozen "foo")` — Return `1` (`u32`) if an existing symbol is frozen,
      otherwise `0` (`u32`); fail if its name is not defined
    + `(immutable-ref VALUE)` — Borrow a string or array as a read-only view;
      the underlying storage remains writable through other mutable owners
    + `(is-immutable-ref VALUE)` — Return `1` when the string or array itself
      is an immutable reference, otherwise `0` (including numbers and void)
    + `(has-immutable-refs VALUE)` — Return `1` when a value is an immutable
      reference or a string array contains any immutable string references
    + `(copy VALUE)` — Make an independent mutable copy of a string or array,
      including deep copies of all string-array elements

    `(var-ref "NAME")` resolves an evaluated string using ordinary scoped
    variable lookup and returns an independently allocated string containing
    the variable’s actual stored name. A global variable such as `foo` yields
    `"foo"`; a local `foo` in a `(do)` at expression index `N` yields
    `"_LOCAL_N_foo"`. Local shadowing is respected. The target must be an
    existing scalar or array symbol (including a variable reset to `TYPE_VOID`);
    labels, native functions, and user-defined function labels are not valid
    targets. Frozen variables can be referenced, but remain immutable.

    A `(var-ref)` result is an ordinary string, **not a distinct reference
    type or a pointer to an array/string allocation**. Name-based operations
    such as `(str-set)`, `(arr-set)`, `(set)`, and `(get)` can use that string
    to access the original variable, even when a called function does not
    inherit the caller’s local scope. Only the name is copied when passing the
    reference; the referenced string or array contents are not copied.
    For example:

    ```lisp
    (fn "replace-first" (args "target") (do
      (str-set target 0 (str-char "x"))))

    (fn "replace-first-element" (args "target") (do
      (arr-set target 0 42)))

    (do
      (local "text" "hello")
      (local "numbers" (arr 1 2 3))
      (replace-first (var-ref "text"))
      (replace-first-element (var-ref "numbers"))
      (assert (== text "xello"))
      (assert (== numbers (arr 42 2 3))))
    ```

    **Reference lifetime and recursion limitations:** `(var-ref)` does not
    keep its target scope alive or capture a specific activation of that
    scope. When a `(do)` exits, its local symbol is reset to `TYPE_VOID`; a
    saved reference string can still name that reset table entry, and a later
    re-entry can reuse it for a different value. It is the caller’s
    responsibility not to use such a stale reference as if it still named the
    original local. This is analogous to managing a borrowed handle manually.

    Also, recursive re-entry into the *same* `(do)` expression reuses the
    same `_LOCAL_N_` names. While the inner activation runs, a reference
    obtained from the outer activation can resolve to the inner activation’s
    local instead. Once recursion unwinds, the outer local is restored.
    Locals in distinct, non-reentered scopes do not have this particular
    collision. For example:

    ```lisp
    (var "saved" "")
    (fn "walk" (args "n") (do
      (local "item" n)
      (if (== n 1) (do
        (override "saved" (var-ref "item"))
        (walk 0)))
      (if (== n 0)
        (assert (== (get saved) 0))))) ; Resolves to the inner `item`.
    (walk 1)
    (assert (== (type-of (get saved)) void)) ; Scope has exited.
    ```

    Returning or passing a reference never makes the referenced variable
    immutable or automatically prolongs its lifetime. The reference string
    itself is runtime-managed and can be reclaimed by manual `(gc)` when
    unreachable; `(var-ref)` does not perform GC.

    `(reset "NAME")` sets an existing scalar or array variable’s value to
    actual `TYPE_VOID`, regardless of its previous type. For arrays, the symbol
    becomes a scalar variable containing `TYPE_VOID`. The symbol remains
    defined, and `(reset)` returns its new void value. Resetting an already
    reset variable is allowed. Undefined names, invalid names, non-variable
    symbols, and frozen variables cause evaluation failures. `(reset)` does
    not unfreeze variables.

    Unlike `(set "NAME" void)` or `(set "NAME" 0)`, `(reset)` does not assign a
    numeric `u32` zero: `void` is the numeric type identifier for `TYPE_VOID`.
    `(reset)` only removes the variable’s reference to any previously held
    dynamic data. It does **not** free memory or call `(gc)` automatically.
    A later manual `(gc)` can reclaim the data only if no other symbols or
    active evaluator values still reference it. See
    [Manual garbage collection](#manual-garbage-collection).

    `(get NAME)` evaluates `NAME` to a string and looks up the corresponding
    symbol, returning the same result as referencing that symbol directly.
    It works with scalar variables, arrays, and labels. It does not invoke
    native functions; use `(call)` for dynamic function calls.
    An invalid name, an undefined symbol, or a native function name causes an
    evaluation failure.

    `(variables)` and `(local-variables)` take no arguments and return
    independent string arrays (`s`) containing actual symbol-table names. All
    occupied symbol-table entries are included, regardless of kind: scalar
    variables, arrays, labels, and native functions, including frozen symbols
    and variables reset to `TYPE_VOID`. Names are not modified or shortened for
    display.

    `(variables)` enumerates the entire global symbol hash table, including
    generated `_LOCAL_N_…` names belonging to active or previously exited
    `(do)` scopes. Both shadowed and shadowing symbols appear, even if one is
    currently inaccessible through ordinary scoped lookup. Local entries
    remain listed after cleanup because their symbol-table slots persist.

    `(local-variables)` enumerates only symbols visible through the current
    `(do)` scope chain, using their exact stored names. A visible local symbol
    such as `_LOCAL_N_x` masks both global `x` and `x` in any outer `(do)`;
    only the innermost visible symbol for that name is listed. Unmasked globals
    are included. A local label also masks a global variable with the same
    unprefixed name. Previously registered locals outside the visible scope
    chain are excluded. At the top level, only global symbols are visible.
    Function calls use definition-site resolution and do not expose locals
    belonging to a hidden caller.

    The results are snapshots: changing or resetting symbols does not modify
    previously returned name arrays. Each string has independent runtime
    storage, so repeated enumeration consumes runtime allocation slots until
    an explicit `(gc)` or evaluation cleanup. Enumeration does not perform GC.
    The names can be used with `(get)`, `(kind-of)`, `(is-frozen)`, and other
    standard-library functions to implement custom filtering and debugging.

    Example:

    ```lisp
    (var "x" 100)
    (do
      (local "x" 20)
      (local "temporary" 42)
      (var "global-from-do" 7)

      ; Includes "x", "_LOCAL_N_x", and "_LOCAL_N_temporary".
      (variables)

      ; Includes "_LOCAL_N_x", "_LOCAL_N_temporary", and "global-from-do",
      ; but excludes the masked global "x".
      (local-variables)
    )
    ```

    Here `N` denotes the corresponding `(do)` expression index. The actual
    returned names contain that numeric index, not the literal letter `N`.

    `(freeze "NAME")` accepts the name of any existing symbol, including
    scalar variables, arrays, labels, and native functions. Calling it more
    than once for the same symbol is allowed. Frozen values remain readable,
    but `(set)`, `(override)`, `(str-set)`, `(str-resize)`, and `(arr-set)`
    fail if they attempt to modify one. `(var)` cannot redefine a symbol,
    frozen or otherwise. `(label)` may re-evaluate a frozen label only when
    the label already points to that exact expression; moving it fails.

    User-defined functions may also be frozen by their public names:
    `(fn "foo" 1) (freeze "foo")` freezes the underlying `_FN_foo` label.
    The function remains callable, but `(fn "foo" 2)` then fails because it
    would redefine that frozen label. `(is-frozen "foo")` reports the same
    frozen state. When a function shares a name with a global symbol (such
    as a native function), the user-defined function takes precedence, just
    as with function calls. A visible local symbol takes precedence over
    both; the underlying function label can always be named explicitly as
    `_FN_foo`.

    The user may freeze standard-library symbols as well. No symbols are
    frozen by default, and a fresh evaluation starts with unfrozen symbols.
    `(is-frozen)` works with variables, arrays, labels, user-defined
    functions, and native functions without changing their frozen state.

    `(kind-of "NAME")` uses the same function-aware name resolution as
    `(freeze)` and `(is-frozen)`: a user-defined function’s public name
    returns `label-kind`, since functions are stored as `_FN_` labels. A
    visible local symbol takes precedence; otherwise, the user-defined
    function takes precedence over an equally named global symbol. The
    underlying `_FN_` name can also be inspected directly.

    Only `(kind-of)`, `(freeze)`, and `(is-frozen)` use this function-aware
    resolution. `(get)`, `(set)`, `(reset)`, string/array accessors, and
    ordinary variable references still resolve actual variable or label names
    in their normal scopes. `(variables)` and `(local-variables)` continue to
    enumerate stored symbol-table names without translating function names.

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

  * <a id="pseudo-local-scopes"></a>Pseudo-local scopes

    + Only `(do)` establishes a pseudo-local scope. The top-level program does
      not provide one; attempting to define a local outside a `(do)` fails.

    + Local variables live in the existing global symbol table. For a `(do)`
      at expression index `N`, `(local "foo" VALUE)` defines or replaces the
      symbol `_LOCAL_N_foo`. Generated names are not reserved and may also be
      accessed explicitly, like ordinary global symbols.

    + `(local "NAME" VALUE)` defines or replaces a variable in the innermost
      active `(do)` scope. It accepts the same scalar and array values as
      `(var)`, independently copying strings and arrays. Unlike `(var)`, it
      replaces an existing unfrozen local, including its type and array size.
      Replacing a frozen local causes an evaluation failure.

    + `(local-label "NAME")` defines or updates a label in the innermost
      active `(do)` scope, using the same `_LOCAL_N_` prefix as local variables.
      Its name can be evaluated dynamically. Existing labels can be revisited
      and redefined following `(label)` rules, including frozen-label checks.
      A name already used by an active non-label local causes an evaluation
      failure. Like other locals, the label is reset and unfrozen when its
      scope exits. `(goto)` and `(get)` already resolve such labels through
      the active local-scope chain, falling back to the global namespace.

    + `(local-str-new "NAME" LENGTH)` creates or replaces a local
      zero-initialized string of the specified byte length.
      `(local-arr-new "NAME" TYPE LENGTH)` similarly creates or replaces a local
      typed array. Existing frozen locals cannot be replaced. These operations
      allocate runtime-managed storage, just like their global counterparts
      `(str-new)` and `(arr-new)`.

    + Direct variable references and name-based operations such as `(get)`,
      `(set)`, `(reset)`, `(str-get)`, `(str-set)`, `(str-resize)`, `(arr-get)`,
      `(arr-set)`, `(arr-size)`, `(arr-type)`, `(freeze)`, `(is-frozen)`, and
      `(kind-of)` search active `(do)` scopes from innermost to outermost, then
      fall back to the unprefixed global name. For `(freeze)`, `(is-frozen)`,
      and `(kind-of)`, a user-defined function’s `_FN_` label takes precedence
      over an equally named global symbol, unless a visible local symbol masks
      it. `(var)` and `(override)` continue to define and replace global
      symbols. Function definitions and `(label)` remain global, while
      `(local-label)` creates a scoped label.

    + When a `(do)` exits, its local symbols remain in the global table but
      their values are reset to actual `TYPE_VOID` and their frozen flags are
      cleared. Cleanup runs after user-defined `(defer)` expressions, including
      on `(leave)`, `(goto)`, `(exit)`, and evaluation failure. Locals created
      by deferred expressions belong to the same enclosing `(do)` scope.

    + Resetting locals does not run `(gc)` or directly free any allocations.
      An explicit `(gc)` can reclaim the old dynamic data when no references
      remain. Values returned by `(do)` and other active evaluated values remain
      valid even after the corresponding local symbol is reset.

    + Local names must fit the generated prefix within `SYMBOL_NAME_SIZE`,
      which is now `64` bytes including the terminating `\0`. Accessing a
      local after its scope has exited is not intended; manually referencing
      its generated name exposes the reset value as ordinary symbol data.

    + Example:

      ```lisp
      (var "x" 100)
      (do
        (local "x" 10)
        (do
          (local "x" 20)
          (assert (== x 20)))
        (assert (== x 10)))
      (assert (== x 100))
      ```

    + Recursive entry into an already active `(do)` expression temporarily
      saves its previous local symbol values and frozen flags. The inner
      activation receives its own values, and the outer values are restored
      when the inner activation exits, including through `(leave)`, `(goto)`,
      `(exit)`, or evaluation failure. Any suspended string or array references
      remain visible to manual `(gc)`; this does not trigger automatic GC.

  * <a id="user-defined-pseudo-functions"></a>User-defined pseudo-functions

    + `(fn "sum-fn" (args "one" "two") (+ one two))` defines a function with
      named arguments. `(sum-fn 10 20)` calls it. The argument declaration may
      be omitted when no named arguments are needed, for example
      `(fn "answer" 42)`.

    + `(fn "name" (var-args) BODY)` accepts any number of arguments;
      `(fn "name" (var-args "foo" "bar") BODY)` requires the named arguments
      first and permits additional ones. Function arguments retain their
      individual types; no heterogeneous array type is required.

    + `(fn)` definitions are allowed only at the top level. Evaluate the
      function name when defining it, allowing dynamically constructed names.
      Register a public, ordinary label named `_FN_<name>` in the existing
      symbol table, pointing to the original function definition expression.
      No separate function symbol kind is introduced.

    + For a function call, look up `_FN_<name>` first; fall back to the native
      function of that name if no such label is defined. Keep normal `(goto)`
      behavior for labels, including labels that follow this naming convention.

    + Make function invocation an evaluator operation, not a synthetic jump to
      a return label. Evaluate call arguments in the caller’s scope, then
      evaluate the existing function body in its definition-site scope, without
      inheriting the caller’s local scopes or generating extra expressions.
      Keep the original expression indices for source-location diagnostics.

    + Give every invocation an implicit outer `(do)` scope, with its own local
      argument bindings and cleanup. Return the function body’s evaluation
      result like an ordinary expression, including from nested calls. Preserve
      and restore the caller’s evaluation and scope context on return or
      failure. Named argument values are copied into locals, so changes to
      their strings or arrays do not modify the caller’s arguments.

    + Every call defines an implicit function-local `argc` (`u32`) containing
      the number of supplied arguments. A user-defined argument named `argc`
      shadows this implicit local. The function-local `argc` also shadows any
      global variable with the same name without modifying it; the global
      variable becomes accessible again after leaving the function’s scope.

    + `(get-arg INDEX)` reads a zero-based argument from the current function
      call, preserving its original type. `INDEX` must be `u32` and within
      the supplied argument count. Calling it outside a function fails.
      `(args)` and `(var-args)` are declarations only, not callable functions.
      Names in argument declarations must be string literals.

    + A function definition returns the previous evaluation result, similarly
      to `(label)`. Calls can be nested in ordinary expressions; fixed-arity
      functions require the declared argument count, while `(var-args)`
      accepts any count at least as large as its required named arguments.
      At most `EVAL_ARG_CAPACITY` arguments can be passed in a direct call.

    + Preserve the existing `(leave COUNT)` semantics inside functions: `COUNT`
      is the number of enclosing `(do)` scopes to exit. Use `(fn-depth)` or
      `(fn-return-depth)` to select the appropriate number of scopes without
      modifying the existing `(leave)` special form. `(return [VALUE])`
      terminates the current function directly, from any nesting depth.

      Note that `(leave)` can leave nested function calls too:

      ```lisp
      (fn "a" (b))
      (fn "b" (c))
      (fn "c" (leave 3 42))

      (assert (== (a) 42))
      ```

    + Call frames preserve arguments, the caller’s last evaluation result,
      and definition-site scope context. Direct and mutual recursion are
      supported: reentrant `(do)` activations save and restore previous local
      symbols, including frozen flags and references to strings and arrays.
      Named string and array arguments remain independent copies, as with
      other function calls. Only the temporary activation snapshots are
      automatically wiped and freed; dynamic language values remain subject
      to explicit `(gc)` and normal evaluation cleanup.

    + `(call-depth)` returns the current user-defined function-call nesting
      depth as `u32`. It returns `0` outside a user-defined function, `1`
      inside the first call, and increases by `1` for every nested call,
      including direct and mutual recursion. Ordinary `(do)` blocks do not
      affect `(call-depth)`; unlike `(depth)`, it counts function invocations,
      not sequence nesting. The result is restored when returning to a caller.

    + `(fn-depth)` returns the **absolute** `(depth)` (`u32`) of the current
      function invocation’s implicit `(do)` scope. It equals the caller’s
      sequence depth at the moment of invocation plus `1`, and remains fixed
      inside explicit `(do)` blocks within the function. Nested calls and
      recursion each have their own absolute function depth. It returns `0`
      outside a function, even inside ordinary top-level `(do)` blocks.

    + `(fn-return-depth)` returns the number of `(do)` scopes to exit to leave
      the **current function**, including its implicit scope. It is `1` when
      evaluating a body with no explicit `(do)`, increases by `1` per nested
      explicit `(do)`, and decreases as those scopes exit. It does not count
      scopes of the caller. It returns `0` outside a function. Both functions
      accept no arguments and return `u32`.

      The two values differ when the current function was called from within
      a `(do)` or another function. `(fn-depth)` is a fixed absolute position
      in the program’s sequence-depth stack, **not** a count of scopes needed
      to return from the current function. Consequently, `(leave (fn-depth))`
      may propagate beyond the current invocation. To return reliably from
      any nested `(do)` within the current function, use
      `(leave (fn-return-depth) VALUE)`; ordinary `(leave)` semantics still
      apply, including `(defer)` execution and local-scope cleanup.

      `(return)` is a separate evaluator special form that exits the current
      user-defined function, without leaving any caller scopes. It takes zero
      or one expression argument and works from its implicit function scope,
      within explicit `(do)` blocks, and in nested expressions. `(return)`
      with no argument returns **`SYM_EMPTY`**, even if the function previously
      evaluated a nonempty result; this differs from bare `(leave)`, which
      forwards the previous result. `(return VALUE)` evaluates `VALUE` once
      and returns that datum, including strings, arrays, labels, and `empty`.
      A failure or a nested control transfer while evaluating `VALUE` takes
      precedence over the outer return.

      Returning unwinds the function’s active `(do)` scopes, runs their
      `(defer)` expressions from innermost to outermost, and cleans up local
      bindings using the normal `(leave)` machinery. A return attempted
      directly by a deferred expression cannot interrupt its enclosing
      sequence. Calling `(return)` outside a function, or supplying more than
      one argument, causes an evaluation failure. `(return)` does not require
      or add an explicit body `(do)` scope.

      For example:

      ```lisp
      (fn "answer" (return 42))
      (assert (== (answer) 42))

      (fn "nothing" (do
        123
        (return)
        (assert false)))
      (assert (== (type-of (nothing)) void))

      (fn "nested" (do
        (do (return "done"))
        (assert false)))
      (assert (== (nested) "done"))
      ```

      For example:

      ```lisp
      (fn "direct" (do
        (assert (== (fn-depth) 1))
        (assert (== (fn-return-depth) 2))
        (leave (fn-return-depth) 42)))
      (assert (== (direct) 42))

      (fn "inspect" (do
        ; The caller below has two active `(do)` scopes, so this
        ; function’s implicit scope has absolute depth 3.
        (assert (== (fn-depth) 3))
        (assert (== (fn-return-depth) 2))
        (do
          (assert (== (fn-depth) 3))
          (assert (== (fn-return-depth) 3))
          (leave (fn-return-depth) 99))))
      (do
        (do
          (assert (== (inspect) 99))))
      ```

      Unlike `(call-depth)`, these functions measure `(do)` scopes rather than
      function-call nesting. Unlike `(depth)`, `(fn-depth)` stays constant
      during a function call and `(fn-return-depth)` excludes caller scopes.
      See `examples/fn-depth.lisp` for nested calls, recursion, and deferred
      cleanup.

    + Recursive function calls are limited to
      `EVAL_FUNCTION_CALL_DEPTH_LIMIT` nested invocations (currently `128`).
      Exceeding the limit causes an evaluation failure instead of unbounded
      recursive calls. **TODO** Consider tail-call optimization separately.

    + A `(goto)` inside a function follows the existing label-resolution rules,
      including local labels visible in the function’s definition-site scopes.
      A jump to a reachable label outside the function body unwinds the function
      and any nested recursive calls, running their defers and local cleanup.
      Jumping into a function body from outside using a label defined within
      that body remains unsupported by the existing jump-path rules.

    + Practical examples:

      Define reusable helpers and call them inside other expressions:

      ```lisp
      (fn "rectangle-area" (args "width" "height") (* width height))

      (assert (== (rectangle-area 4 5) 20))
      (assert (== (+ (rectangle-area 3 7) 10) 31))
      ```

      Return early using `(leave COUNT VALUE)`. Here, `2` leaves the explicit
      body `(do)` and the function’s implicit `(do)` scope:

      ```lisp
      (fn "first-nonzero" (args "a" "b") (do
        (if (!= a 0) (leave 2 a))
        b
      ))

      (assert (== (first-nonzero 0 9) 9))
      (assert (== (first-nonzero 7 9) 7))
      ```

      Recursive functions preserve each invocation’s argument values:

      ```lisp
      (fn "factorial" (args "n")
        (if (<= n 1)
          1
          (* n (factorial (- n 1)))
        )
      )

      (assert (== (factorial 6) 720))
      ```

      Process a variable number of arguments using `argc` and `(get-arg)`.
      `(apply)` can supply additional arguments from a typed array:

      ```lisp
      (fn "sum-all" (var-args) (do
        (local "total" 0)
        (local "index" 0)
        (label "sum-all-next")
        (if (< index argc) (do
          (set "total" (+ total (get-arg index)))
          (set "index" (+ index 1))
          (goto sum-all-next)
        ))
        total
      ))

      (assert (== (sum-all 10 20 30) 60))
      (assert (== (apply "sum-all" 10 (arr 20 30)) 60))
      ```

      Named string and array arguments are copied into function-local storage.
      Modifying a named string argument does not modify the caller’s variable:

      ```lisp
      (var "original" "hello")
      (fn "capitalize-first" (args "text") (do
        (str-set "text" 0 "H")
        text
      ))

      (assert (== (capitalize-first original) "Hello"))
      (assert (== original "hello"))
      ```

      Function names can be constructed when defining a function and resolved
      dynamically when calling it:

      ```lisp
      (fn
        (str "make-" "greeting")
        (args "person")
        (str "Hello, " person "!")
      )

      (assert (== (call (str "make-" "greeting") "Ada") "Hello, Ada!"))
      ```

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

    + Array equality

      `(== ARRAY1 ARRAY2)` and `(!= ARRAY1 ARRAY2)` also accept evaluated
      arrays. Both arrays must have the same element type (`u32`, `i32`, `f32`,
      or `s`); different element types cause an evaluation failure, even for
      empty arrays. Mixing an array and a scalar is also an evaluation failure.

      Arrays of the same element type compare equal when their lengths match
      and all elements at corresponding indexes are equal. Different lengths
      produce `false` for `(==)` and `true` for `(!=)`.

      String elements use the same case-sensitive, length-aware byte comparison
      as scalar strings, including embedded `\0` bytes. Numeric elements use
      their respective scalar equality rules. The result is always `u32`.
      The other comparison operators (`<`, `<=`, `>`, `>=`) do not accept
      arrays.

      + `(== (arr 1 2 3) (arr 1 2 3))` → `true`
      + `(!= (arr "a" "b") (arr "a" "c"))` → `true`
      + `(== (arr 1 2) (arr 1 2 3))` → `false`

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
      + `(fn "foo" 1) (kind-of "foo")` → equals to `label-kind`
        (a function’s public name refers to its `_FN_foo` label for
        introspection; `(kind-of "_FN_foo")` gives the same result)
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

- Dynamic function invocation

  `(call FUNCTION-NAME ARG…)` evaluates `FUNCTION-NAME` to a string,
  looks up a user-defined `_FN_` label first, then a native function,
  and invokes it with the remaining evaluated arguments.
  The function’s result becomes the result of `(call)`:

  ```lisp
  (call "str-size" "hello") ; Returns 5
  (call (str "print" "ln") "Hello!") ; Prints Hello!
  (var "fn" "println")
  (call fn "Hello again!")
  ```

  The name must refer to a registered user-defined pseudo-function or an
  existing native function (`SYM_NATIVE_FUNCTION`). All arguments are evaluated
  eagerly, so `(call)` **cannot invoke the evaluator’s
  [special forms](#special-forms)** such as `(if)` or `(do)`.
  An undefined or non-callable name causes an evaluation failure.

  `(apply FUNCTION-NAME [FIXED-ARG…] ARRAY)` invokes a function like `(call)`,
  but expands the elements of its final array into individual arguments.
  The final array is required; fixed arguments are optional.

  ```lisp
  (apply "+" 10 20 (arr 1 2 3)) ; Equivalent to (call "+" 10 20 1 2 3)
  (apply "str" "hello" (arr " " "world")) ; Returns "hello world"

  (arr-new "empty-tail" u32 0)
  (apply "+" 10 20 empty-tail) ; Equivalent to (call "+" 10 20)
  ```

  The function name, fixed arguments, and final array are evaluated from
  left to right before invocation. The array must be an evaluated `SYM_ARRAY`;
  its elements are passed in order, preserving their element types and string
  lengths, including embedded `\0` bytes. Fixed arguments may have different
  types from the array elements. Only the final array is expanded; preceding
  arrays remain ordinary arguments.

  Expanded calls share `EVAL_ARG_CAPACITY` (currently `32`) with ordinary
  calls, counting the function name as one argument. Exceeding that limit
  causes an evaluation failure. No dynamically growing argument storage is
  used. Empty arrays are allowed and contribute no arguments.

  `(apply)` reuses `(call)` dispatch, including user-defined pseudo-functions,
  native functions, return values, and error behavior. It cannot invoke
  evaluator special forms. String elements are not independently copied by
  the expansion itself; ordinary callee argument-binding semantics still
  apply, and the expanded values remain GC-rooted during invocation.

- <a id="special-forms"></a>Special forms

  Most stdlib functions live in a shared variable namespace, but there are a few
  special evaluator forms (exceptions to this rule):

  - `(do)`
  - `(if)`
  - `(and)`
  - `(or)`
  - `(leave)`
  - `(return)`
  - `(defer)`
  - `(fn)`

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

- <a id="parse-time-includes"></a>Parse-time includes

  Lispico programs can insert expressions from other source files with
  `(include "PATH")`. The path must be a **literal string**, and the include
  is expanded during parsing, not evaluation. For example, if
  `lib/arguments.lisp` contains the expressions `2`, `3`, and `4`:

  ```lisp
  (+ 1 (include "lib/arguments.lisp") 5) ; Equivalent to (+ 1 2 3 4 5)
  ```

  An include contributes **zero or more sibling expressions** at its exact
  location, anywhere the parser accepts expressions: top-level programs,
  `(do)` bodies, nested lists, and argument lists. No wrapper expression or
  implicit scope is introduced. An empty file contributes zero expressions;
  an included file must contain complete, independently parseable expressions
  (not half of an open list). Ordinary scope and evaluation rules apply to the
  resulting expression sequence, including restrictions on where `(fn)` may
  execute.

  Relative paths are resolved against the **containing source file’s
  directory**, including inside nested includes, regardless of the current
  working directory. When the main source is stdin (`-` or `/dev/stdin`),
  includes in that source are relative to the current working directory.
  Absolute paths are also accepted. String escapes follow the normal Lispico
  string syntax; empty paths and embedded null bytes are rejected.

  Repeated includes produce repeated expressions, just like copying the same
  source text more than once. Circular includes are parsing errors. The POSIX
  desktop parser detects cycles in the **active include chain** using the
  opened file’s filesystem identity (`st_dev` and `st_ino`), including cycles
  through symbolic or hard links; an earlier, completed include does not
  prohibit including the same file again. There are no namespaces, module
  caches, or runtime include operations.

  The desktop interpreter invokes `parse_source_with_includes_diagnostic()`
  (declared in `src/parse.h`) for structured parsing errors. The simpler
  `parse_source_with_includes()` entry point remains available without the
  additional diagnostics output. The original in-memory `parse_source()` remains
  available without filesystem expansion. Both share the existing expression
  storage. Source maps are **optional**: with source mapping enabled, each
  included source receives its own ID and its buffer is retained in
  `SourceRegistry` until `clean_source_registry()`. With `--no-source-map`,
  included buffers are freed immediately after parsing and no source registry
  is required. The parsed expressions do not store source text or paths.

  Includes that cannot be opened, contain invalid syntax, exceed the source
  capacity, or form a cycle cause a parsing failure. With source mapping
  enabled, both parsing and evaluation failures identify the actual source
  path, line, and column, and render an excerpt from that file. Parsing errors
  at end-of-file display the final line (including an empty trailing line).
  An error in an included file is followed by an `Included from` trace, from
  its immediate includer outward to the main script. Each include occurrence
  has its own ancestry, even when the same file is included multiple times.
  If a parsing include chain exceeds the diagnostic buffer, Lispico retains
  complete trace lines and reports that additional include sites were omitted.

  Include traces for **parsing errors** do not require source maps: they are
  collected as recursive parsing unwinds, before the included source buffers
  are released. With `--no-source-map`, parsing diagnostics still report the
  source pathname, location, and include chain without an excerpt. Evaluation
  errors without source mapping continue to report expression indices rather
  than source locations. Include provenance and source text are diagnostic
  metadata only and are not embedded in the expression representation.

  For example, if `main.lisp` includes `lib/helpers.lisp`, which includes
  `lib/broken.lisp`, an evaluation error in the latter is displayed as:

  ```text
  [FAIL] Evaluation failed: lib/broken.lisp:1:6: Undefined variable
  1 | (+ 1 missing)
    |      ↑
  Included from lib/helpers.lisp:2:1
  Included from main.lisp:1:1
  ```

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

  + `(leave [COUNT [VALUE]])` — Immediately terminates one or more active
    `(do)` sequences, optionally forwarding an explicit resulting value.

  + `(leave)` is an evaluator special form, not an ordinary native function.

  + `COUNT` is an optional evaluated expression that must produce a `u32` value.

    + When omitted, `COUNT` defaults to `1`, terminating the nearest `(do)`.
    + A positive `COUNT` terminates exactly that many enclosing `(do)`
      sequences.
    + `COUNT` greater than `(depth)` causes an evaluation failure.
    + `COUNT` equal to `0` causes an evaluation failure.

  + `VALUE` is an optional evaluated expression, allowed only when `COUNT` is
    provided. Its result can be any datum, including `empty`. `COUNT` is
    validated before evaluating `VALUE`.

  + `(leave)` requires at least one active `(do)` sequence. Calling it at the
    top level causes an evaluation failure.

  + Expressions following `(leave)` in any terminated sequence are skipped.
    Evaluation resumes after the outermost terminated `(do)` expression.

  + Without `VALUE`, `(leave)` preserves the last successfully evaluated value.
    If there is no previous nonempty value, the resulting value is `SYM_EMPTY`.
    When `VALUE` is provided, its result replaces the previous value and is
    forwarded through all terminated sequences. For example,
    `(leave 1 (last-val))` is equivalent to `(leave 1)`.

  + `(leave)` can be used inside `(if)`, `(and)`, `(or)`, and native function
    arguments. It interrupts the current expression and propagates through
    the requested number of enclosing `(do)` sequences.

  + If evaluating `VALUE` triggers another `(leave)`, the inner `(leave)`
    interrupts the outer one before it can initiate its own leave. Only the
    inner leave count and resulting value take effect; the counts are not
    combined.

  + `(depth)` is restored as each sequence is terminated.

  + Examples:

    ```lisp
    ; Leave only the nearest sequence
    (do
      (leave)
      (println "Never printed"))

    ; Return an explicit value from the nearest sequence
    (assert (== (do
      (leave 1 (+ 20 22))
      (println "Never printed")) 42))

    ; The inner (leave) interrupts the outer (leave)
    (assert (== (do
      (leave 1 (leave 1 100))
      200
    ) 100))

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

* Returning from functions

  + `(return [VALUE])` — Leave the current user-defined function, unwinding
    every scope belonging to that invocation without leaving caller scopes.

  + `(return)` returns `SYM_EMPTY`, rather than forwarding a previous value.
    `(return VALUE)` evaluates and returns the value once. Both forms run
    pending deferred expressions and clean up locals through normal scope exit.

  + `(return)` requires an active user-defined function call and accepts at
    most one argument. It is an evaluator special form, not a native function.
    See the user-defined function section and `examples/return.lisp`.

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
      + `(do (defer 456) (leave 1 123))` returns `123`.
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

## Command-line interface

The desktop interpreter parses its command line without third-party argument
parsing libraries. The reusable parser is in `src/parse-cli-args.{c,h}`.

```text
Usage: lispico [OPTIONS] [SOURCE_FILE] [-- SCRIPT_ARGS…]

Options:
  -s, --source-file FILE         Read source from FILE (or “-” for stdin)
      --source-file=FILE         Alternative form for the source path
  -p, --print-result             Print the final evaluation result
      --no-source-map            Use expression indices instead of line and column
      --source-context-before N  Show N preceding source lines (default: 3)
      --source-context-before=N  Alternative form of --source-context-before
      --source-highlight=MODE    Source renderer: bat (default), none or sh
      --source-highlight MODE    Alternative form of --source-highlight
      --source-highlight-sh=CMD  Shell command for sh mode
      --source-highlight-sh CMD  Alternative form of --source-highlight-sh
      --var NAME TYPE:VAL        Define a mutable variable (repeatable)
      --arr NAME TYPE N VALUE…   Define an array from N arguments
      --arr-new NAME TYPE N      Define a zero-initialized array of length N
      --arr-set NAME IDX VAL     Set an element of a previously defined array
      --freeze NAME              Freeze a previously defined variable or array
  -h, --help                     Display this help and exit
```

Without a source file, Lispico reads `/dev/stdin`. Exactly one source file may
be specified, either positionally or through `--source-file` / `-s`; specifying
more than one is an error. Options may appear before or after a positional
source file, until `--` is encountered.

Use `--source-file` or `-s` for a file whose name begins with a dash, to avoid
interpreting it as a command-line option:

```sh
lispico --source-file=-my-script.lisp
lispico -s -my-script.lisp
```

The special path `-` also means standard input (`/dev/stdin`):

```sh
lispico -s -
```

The `-p` and `-h` flags may be grouped with other short options. `-s` requires
a separate following file-path argument and **must be the final letter** in
any short-option group. For example, `-ps script.lisp` is valid, while
`-psh script.lisp` and `-sp script.lisp` are invalid. The joined forms `-sFILE`
and `-psFILE` are not supported; use `-s FILE` or `-ps FILE` instead.

`-h` / `--help` displays usage information and exits with code `0` without
reading or evaluating a source file. Invalid arguments display an error and
the same usage information on stderr, then exit with code `1`.

Evaluation failures report the one-based source line and byte column of the
innermost failing expression by default. Source locations are kept in an
optional table separate from the expression storage. Each location also holds
an ID identifying its source file. The host may register source file paths and
buffers in a separate `SourceRegistry` (`src/source-registry.{c,h}`). The
main source remains host-owned under source ID `0`; included source paths and
buffers are registry-owned when mapping is enabled and freed when the registry
is cleaned. The desktop parser assigns a distinct source ID to each included
file occurrence. Internally, top-level and list-child sequences are built by
appending expression ranges. Ordinary expressions contribute one node;
parse-time `(include)` expands to zero or more sibling nodes without a wrapper.

Use `--no-source-map` to disable the location table and report
`Expression Index N:` instead of `Line N Column N:`. The source registry is
also unnecessary when source mapping is disabled. Failures before an expression
is evaluated have no location prefix. Source mapping does not change evaluation
behavior or parser errors.

When source mapping is enabled, Lispico also prints the failing source line
and up to three lines immediately preceding it, followed by an `↑` marker
under the failing expression. For example:

```text
[FAIL] Evaluation failed: Line 3 Column 8: Undefined variable
1 | (var "x" 123)
2 | (do
3 |   (+ x missing))
  |        ↑
```

Use `--source-context-before=N` to change the number of preceding lines.
For example, `--source-context-before=0` shows only the failing line,
while `--source-context-before=5` shows up to five lines before it.
`--source-context-before N` is also accepted. When `--no-source-map` is
specified, no source excerpt is printed. Positions are byte-based; tabs
are expanded to four spaces and ASCII control characters are escaped in
excerpts. The reusable renderer is in `src/source-location.{c,h}`.

### Source highlighting

By default, Lispico tries to use `bat` to syntax-highlight the excerpt. If `bat`
is unavailable, fails, or returns empty output, Lispico silently falls back to
its own plain source renderer. `bat` only provides ANSI syntax colors: Lispico
still prints the line numbers, context, and `↑` marker. Before invoking `bat`,
Lispico expands every source tab to exactly four spaces, matching the built-in
arrow alignment. It sends the entire source through standard input so
highlighting can retain the context of multiline strings or comments, while
requesting only the relevant lines. The `bat` preset suppresses the
highlighter’s stderr. The file type is passed to `bat` as its `--language`
argument. The Lispico CLI uses `lisp`; other frontends can supply their own
file type through `print_source_highlight()`.

```sh
lispico --source-highlight=bat program.lisp
lispico --source-highlight=none program.lisp
```

The selected renderer can also come from `LISPICO_SOURCE_HIGHLIGHT`. The
CLI option always takes priority over the environment variable, and the
built-in default is `bat`. Supported values are `bat`, `none`, and `sh`.
The separate-argument form `--source-highlight MODE` is also supported.
For reproducible plain-text diagnostics in tests, use
`--source-highlight=none`.

#### Custom shell renderer

Use `--source-highlight=sh` to let an external shell expression render the
**entire excerpt**, including any line numbers, colors, and column marker.
Provide the expression with `--source-highlight-sh=COMMAND`,
`--source-highlight-sh COMMAND`, or `LISPICO_SOURCE_HIGHLIGHT_SH`. The CLI
command takes priority over the environment command. Selecting `sh` without
a nonempty command is a configuration error.

The command is executed using C’s `system()` shell-command interface in a
separate child process. It receives the **unchanged original source** on
standard input, and its standard output is directed into Lispico’s diagnostic
stderr stream. Its stderr is inherited and is **not** suppressed. The child
receives additional context variables without changing Lispico’s own process
environment:

| Variable                             | Description                                                         |
| ---                                  | ---                                                                 |
| `LISPICO_SOURCE_CONTEXT_BEFORE`      | Requested number of preceding lines                                 |
| `LISPICO_SOURCE_CONTEXT_LINE`        | One-based failing source line; last line of the requested excerpt   |
| `LISPICO_SOURCE_CONTEXT_COLUMN`      | One-based byte column of the failing expression                     |
| `LISPICO_SOURCE_CONTEXT_BYTE_OFFSET` | Zero-based source byte offset                                       |
| `LISPICO_SOURCE_CONTEXT_FIRST_LINE`  | First line of the requested excerpt, clamped to line 1              |
| `LISPICO_SOURCE_CONTEXT_FILE_TYPE`   | Source file type supplied by the frontend (e.g. `lisp`, `confetti`) |

`LISPICO_SOURCE_CONTEXT_FIRST_LINE` saves custom commands from calculating
the context range themselves. For example, with three preceding lines and an
error on line 2, the requested range is lines **1–2**, not a nonexistent
line −1 through 2. The command can use
`--line-range="$LISPICO_SOURCE_CONTEXT_FIRST_LINE:$LISPICO_SOURCE_CONTEXT_LINE"`
directly, or ignore the requested range and render any content it chooses.

For example, this shell renderer uses `bat`’s native whole-line highlighting
and adds a separate `↑` using the supplied error column:

```sh
export LISPICO_SOURCE_HIGHLIGHT=sh
export LISPICO_SOURCE_HIGHLIGHT_SH='
  bat \
    --language="$LISPICO_SOURCE_CONTEXT_FILE_TYPE" \
    --color=always \
    --paging=never \
    --style=plain \
    --wrap=never \
    --highlight-line="$LISPICO_SOURCE_CONTEXT_LINE" \
    --line-range="$LISPICO_SOURCE_CONTEXT_FIRST_LINE:$LISPICO_SOURCE_CONTEXT_LINE" \
    -
  printf "%*s↑\n" "$((LISPICO_SOURCE_CONTEXT_COLUMN - 1))" ""
'

lispico program.lisp
```

`bat` supports highlighting an entire line but does not provide a native
column marker. The example’s `printf` uses byte columns, so its arrow is
intended for ASCII source without tabs. Custom renderers that expand tabs
or display wide Unicode characters should adjust their marker themselves.
Unlike the `bat` preset, `sh` receives the original tabs unchanged.

The shell expression is executable code; only use commands you trust. If it
fails, Lispico keeps any output and inherited stderr messages, adds a warning
with the command’s exit status, and **does not fall back** to plain rendering.
The highlighter’s status does not change Lispico’s evaluation exit code.
`--no-source-map` takes precedence over all highlighting settings: no
highlighter is run and the expression-index diagnostic remains unchanged.
The process launcher is in `src/source-highlight.{c,h}`.

### Final evaluation result

The interpreter **does not print the final result by default**. Printing from
explicit functions such as `(println)` still works. Use `-p` or
`--print-result` to display the result of normal evaluation or `(exit)`, using
the usual `SYM_VARIABLE(…)`, `SYM_ARRAY(…)`, or `SYM_EMPTY` representation.
The flag does not change the program’s exit status or evaluation error output.

### Initial variables

Use `--var NAME TYPE:VALUE` to define variables before evaluation. Numbers
are mutable; CLI-provided strings and arrays are borrowed immutable references
rather than runtime copies. Variable bindings remain replaceable using
`(override)`. Use `--freeze NAME` to prevent rebinding a symbol before
the script runs, using the same rules as `(freeze "NAME")`.

```sh
lispico \
  --var count u32:1_000 \
  --var signed i32:-123 \
  --var fraction f32:1.25 \
  --var converted i32:123 \
  --var decimal f32:123 \
  --var enabled u32:true \
  --var negative-flag i32:false \
  --var ratio f32:true \
  --var message 's:Hello, world!' \
  --var limit u32:100 \
  --freeze limit \
  -s program.lisp
```

Supported type prefixes are `u32:`, `i32:`, `f32:`, and `s:` (string). Numeric
values follow Lispico’s native numeric grammar, including underscores and
exponents, and are checked against the requested type’s range. Explicit
conversion is permitted: `i32:123` and `f32:123` are valid, but `u32:-123` and
`u32:123.456` are not. The lowercase pseudo-booleans `true` and `false` work
for all three numeric types, becoming typed `1` and `0` respectively.

For strings, only the **first** colon separates the type from the value.
Therefore `s:https://example.com:8080/` is valid. An empty string is `s:`.
Shell quoting controls spaces; no extra quoting or escape syntax is applied to
the string contents by the CLI parser.

Duplicate CLI definitions (including an array and a scalar with the same name)
are an error. Command-line variables and arrays take precedence over built-in
symbols, including native functions, `argc`, `argv`, and `args`: they are
deliberately registered last, and the user is free to override them. The
language literal `empty` is not a symbol and cannot be defined as a variable.

### Initial arrays

Use `--arr NAME TYPE COUNT VALUE…` to define a fixed-length array populated
from exactly `COUNT` separate command-line arguments:

```sh
lispico \
  --arr numbers u32 6 1 2 3 4 5 6 \
  --arr names s 3 'Alice' 'Bob' 'Charlie' \
  program.lisp
```

The supported element types are `u32`, `i32`, `f32` and `s`. Numeric elements
use the same conversions and pseudo-booleans as `--var`. String elements are
passed literally, without an `s:` prefix or any special escaping imposed by
Lispico. An array can be empty: `--arr numbers u32 0`.

The element count is important: while consuming those elements, the parser
does not interpret `--`, `--help`, or any other option-looking string as an
option. For example, the following defines four string elements:

```sh
lispico --arr values s 4 '[' ']' '--help' '' program.lisp
```

Alternatively, use `--arr-new NAME TYPE LENGTH` for a zero-initialized array
and `--arr-set NAME INDEX VALUE` to populate selected zero-based indices:

```sh
lispico \
  --arr-new numbers u32 4 \
  --arr-set numbers 0 123 \
  --arr-set numbers 1 321 \
  --arr-set numbers 3 567 \
  program.lisp
```

The resulting array contains `[123, 321, 0, 567]`. Missing numeric elements
are zero; missing string elements are empty strings. `--arr-set` requires an
array already declared earlier on the command line, rejects invalid indices,
and uses that array’s declared type to parse its value. It also works on arrays
created with `--arr`.

These options work naturally with Bash arrays, without any array-specific
string serialization or escaping:

```bash
numbers=(10 20 30 40)

lispico \
  --arr numbers u32 "${#numbers[@]}" "${numbers[@]}" \
  program.lisp

values=(
  'hello, world'
  'some [text]'
  '--help'
  ''
)

lispico \
  --arr values s "${#values[@]}" "${values[@]}" \
  program.lisp
```

### Freezing command-line definitions

`--freeze NAME` freezes the **symbol binding** of a previously declared CLI
scalar or array. This is distinct from the immutable-reference flag on
borrowed CLI string and array values.
Option order matters: freezing a name before its `--var`, `--arr` or `--arr-new`
definition fails. Once frozen, the array cannot be modified by later
`--arr-set` options, and the script cannot change it either.

```sh
lispico \
  --var limit u32:100 \
  --freeze limit \
  --arr-new numbers u32 4 \
  --arr-set numbers 0 123 \
  --freeze numbers \
  program.lisp
```

Only definitions made on the command line can be targeted by `--freeze`; it
does not look up built-in symbols. CLI strings and arrays are borrowed without
copying their underlying data; this is independent of `--freeze` and prevents
in-place mutation through the borrowed values during evaluation.

### Original arguments and script arguments

The process arguments remain available unchanged as `argc` (a `u32`) and
`argv` (an array of strings), including the executable name, interpreter
options, source file and `--` separator. A separate `args` array contains
**only** the arguments after the first standalone `--`. The separator itself
is excluded. When no separator is present, `args` is an empty string array.

For example:

```sh
lispico -ps program.lisp --var answer u32:42 -- --debug 'two words'
```

The script initially sees:

```text
argc = 9
argv = [
  "lispico", "-ps", "program.lisp", "--var", "answer", "u32:42",
  "--", "--debug", "two words"
]
args = ["--debug", "two words"]
```

Everything following `--` is passed through unchanged and never interpreted
as an executable option. Source-file selection must therefore happen before
`--`. These arrays use separate descriptor storage but borrow their string
contents from the process arguments. Both arrays and their string elements are
**immutable references** during Lispico evaluation; use `(override)` with a new
array to replace a binding rather than modifying the borrowed storage.

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
$ ./lispico -p <<< '(println "hello")'
hello
SYM_EMPTY

$ ./lispico -p <<< '(println "hello")(+ 10 5)(+ (last-val) 3)'
hello
SYM_VARIABLE(TYPE_U32(18))

$ ./lispico -p examples/hello-world.lisp
Hello, World!
SYM_EMPTY

$ ./lispico -p <<'LISP'
(var "foo" 123)
(set "foo" (+ foo 3))
(type f32 foo)
LISP
SYM_VARIABLE(TYPE_F32(126.000000))
```

Or try all examples (note that test environment variables required for some
examples):

```sh
(set -eu; . tests/examples.env; find examples/ -maxdepth 1 -type f -name '*.lisp' | while read -r f; do (set -x; ./lispico -p "$f") done)
```

You can also just build `lispico` derivation (or add it to your NixOS config):

```console
$ nix-build -A lispico

$ result/bin/lispico -p <<< '(println "hello")'
hello
SYM_EMPTY
```

## Immutable references and shared backing storage

An **immutable reference** is a **read-only view** of existing string or array
storage. The view may not be modified through its own variable, but the
original mutable owner may modify the same storage. This is *not* an immutable
snapshot, copy-on-write, or a frozen symbol.

- `(immutable-ref STRING/ARRAY)` returns an immutable view **without allocating**.
  Numbers, `TYPE_VOID`, labels, and unsupported kinds are rejected.
- `(is-immutable-ref VALUE)` checks **only the value itself**, not string
  elements inside an array. Non-string/non-array values return `false`.
- `(has-immutable-refs VALUE)` checks the value and any string elements inside
  an array. Non-string/non-array values return `false`.
- `(copy STRING/ARRAY)` creates a **new, independent mutable value**, even when
  the source is an immutable reference. All string elements are copied deeply
  into writable, NUL-terminated storage. Numbers, `TYPE_VOID`, and other kinds
  are rejected rather than silently returned unchanged.

Arrays can be mutable while individual string elements contain immutable
references. The containing array can still replace those elements using
`(arr-set)`. No implicit conversion makes the containing array immutable.

```lisp
(var "text" "hello")
(var "view" (immutable-ref text))
(assert (is-immutable-ref view))
(assert (== (is-immutable-ref text) false))

(str-set "text" 0 (str-char "H"))
(assert (== view "Hello")) ; Live view of the same backing bytes.

; (str-set "view" 0 65) ; Evaluation failure: immutable reference.
; (set "view" "other") ; Evaluation failure: in-place string write.
(override "view" "other") ; Rebinding the variable is allowed.

(var "words" (arr (immutable-ref "first") "second"))
(assert (== (is-immutable-ref words) false))
(assert (has-immutable-refs words))
(arr-set "words" 0 "replacement")
(assert (== (has-immutable-refs words) false))
```

### Explicit mutable copies

`(copy)` takes exactly one string or array. It never returns an immutable
reference. Unlike ordinary variable assignment, which preserves immutable
references without copying, `(copy)` always constructs independent mutable
storage (apart from the ordinary NULL-backed representation of an empty value).
For string arrays, every string element receives its own mutable copy,
regardless of whether that element was an immutable reference. The returned
array and all its string elements therefore report `false` for
`(is-immutable-ref)` and `(has-immutable-refs)`.

```lisp
(var "text" "hello")
(var "view" (immutable-ref text))
(var "independent" (copy view))

(str-set "text" 0 (str-char "H"))
(assert (== view "Hello")) ; The read-only view sees the source’s write.
(assert (== independent "hello")) ; The copy does not share the data.
(str-set "independent" 0 (str-char "w"))
(assert (== independent "wello"))

(var "words" (arr (immutable-ref "first") "second"))
(var "writable-words" (copy (immutable-ref words)))
(assert (== (is-immutable-ref writable-words) false))
(assert (== (has-immutable-refs writable-words) false))
(arr-set "writable-words" 0 "replacement")
(assert (== (arr-get words 0) "first"))
```

Copies are length-based: a string may contain embedded `\0` bytes, and a
non-NUL-terminated read-only view is copied with a new trailing `\0` byte.
`(copy)` uses runtime-managed allocations and does not trigger `(gc)`.
Assigning the resulting mutable value to another variable still follows the
existing **copy-by-default** rules, so that assignment can create another copy.

Mutable string and array values continue to receive independent copies when
bound to variables or user-function parameters. **Immutable references share
storage**, preserving their flags, backing pointers, and nested string-element
flags through assignments, argument binding, and array-element access.
Constructors such as `(str)` still create new mutable string data; the
explicit `(copy)` operation makes an independent mutable value on demand.
String-array constructors allocate their descriptor array but preserve any
immutable string-element references without allocating their byte buffers.

The immutable-reference flag belongs to the **reference**, not to the source
allocation. `(freeze "NAME")` instead prevents rebinding the named symbol.
A mutable symbol can hold an immutable reference and later be rebound with
`(override)` or `(local)`. `(str-resize)` replaces the named string’s storage
with a newly allocated writable string, so existing views continue referring
to the old allocation. In-place writes through `(str-set)` and string `(set)`,
and element writes through `(arr-set)`, reject immutable views.

`String` and `Array` carry `base` and `immutable_ref` fields; `String` also
carries `nul_terminated`. `data` identifies the first visible byte or element,
whereas `base` identifies the beginning of the original backing allocation.
For this stage, both pointers coincide; interior-pointer zero-copy slices are
planned for a later stage. Non-NULL strings are still NUL-terminated.

The existing manual collector uses each live value’s **`base`** pointer to
mark runtime allocations. It also traces individual string elements in arrays,
GC root frames, evaluation results, and suspended recursive local bindings.
The collector never frees parser-owned expression strings, host-owned CLI
buffers, or symbol-name storage. `(gc)` remains explicit and there is no
reference counting or copy-on-write.

**Lifetime:** An immutable view keeps *runtime-managed backing allocations*
reachable through `(gc)` as long as the view remains reachable. It does not
extend the lifetime of buffers supplied by a C host. Host-provided immutable
references require their backing memory to remain alive for the duration of
use. Changing a source variable by allocating replacement storage does not
retarget an existing view; modifying the original backing bytes through a
writable alias remains visible.

**CLI arguments:** CLI-defined strings and arrays now borrow their original
argument storage as immutable references. String-array elements borrow their
individual argument strings too. The built-in `argv` and `args` arrays, and
their string elements, are also immutable references. Numeric CLI variables
remain ordinary mutable numbers. CLI `--arr-set` initialization still works
*before* evaluation, but attempting to mutate these array or string views from
a Lispico script fails. Their symbol bindings can still be replaced using
`(override)` unless explicitly frozen using `--freeze`.

## Memory management and cleanup

Lispico uses **evaluation-lifetime memory allocation**. Dynamically allocated
strings and arrays are tracked by the evaluator and remain allocated until
evaluation cleanup, unless explicitly reclaimed with `(gc)`.

There is no automatic garbage collection. Runtime values may be reclaimed
manually, keeping memory management predictable on embedded platforms.

### Manual garbage collection

`(reset "NAME")` is a way to drop an existing variable’s reference to a
string or array allocation by replacing its value with `TYPE_VOID`. It does
not perform garbage collection: a later explicit `(gc)` may reclaim that
allocation only when it has no other references. Resetting a symbol does not
invalidate values still held by other variables or active expressions.

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
- **Suspended local bindings:** Saved symbols in recursive `(do)` scope
  activations, including function scopes hidden from normal name lookup.

For scalar strings, the collector marks the runtime allocation identified by
the `String.base` pointer. A string view can start inside that allocation;
its visible `data` pointer does not have to match the allocation start.

For arrays, the collector marks `Array.base`, which identifies the backing
storage for its elements. String arrays additionally mark each visible
`String.base` pointer, even when an element is an interior view of another
allocation. Zero-initialized string elements with NULL backing pointers do
not reference any runtime allocations. The collector does not trace string
elements outside an array view’s visible range.

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

Evaluator stack frames and temporary argument/value descriptors are explicitly
wiped after they are no longer needed. Frames for `(do)` scopes, user-defined
function calls, and GC roots link to earlier active frames, but do not own them.
Each frame is unlinked and wiped independently when its evaluation finishes;
following those links to wipe earlier frames would corrupt the caller’s state.
Similarly, wiping a temporary string or array descriptor does not wipe the
allocation it references. Referenced data remains available until an explicit
`(gc)` reclaims it or evaluation cleanup releases it.

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
$ tests/run-cli.sh
$ tests/run-source-highlight.sh
$ tests/run-c.sh
```

The standalone parser tests are in `tests/c/parse-cli-args.c` and run with
`tests/run-c.sh`. The executable integration tests in `tests/run-cli.sh` check
argument parsing, typed scalars and arrays, interpolation, freezing, and
CLI error behavior. `tests/run-source-highlight.sh` tests the `bat` preset
with a fake executable, including tab conversion and silent fallback, as well
as shell execution, context variables, environment overrides, and warnings.
All are also run in CI.

Measure time and memory usage for all examples individually:

```sh
nix-shell --run './build.sh && tests/measure-examples.sh'
```

## Development environment

### [direnv](https://direnv.net) example

Create `.envrc`:

```sh
use nix
```

### Local Vim configuration example

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
