# Simple LISP-ish Interpreter Written in C

**WORK IN PROGRESS: Not ready to use.**

This is not really a conventional LISP interpreter, only relevant in terms of
syntax (for the sake of parser implementation simplicity). It’s more like a
simple low-level calculation DSL language.

This allows DSL-like computations to be parsed and evaluated dynamically in
embedded platforms such as Raspberry Pico as part of a firmware for it written
in C.

## Features

For the sake of keeping the implementation is very simple there is very little
subset of features:

- Global state stored in a single static hash-table.

  There are no lexical-like name scopes. If you need some level of name scoping
  you just name prefixes for example.

- Primitive data types

  * `uint32`
  * `int32`
  * `float` (32-bit `float`)

  Non-negative numbers are defaulting to `uint32`, negative ones defaulting to
  `int32`, fractional numbers default to `float`. When you want specific type
  for a number you can use `(type)` expression:

  * `(type int32 123)`
  * `(type float 123)`

  Same `(type)` can be used for type-conversion
  (be aware of type bounds and number overflows):

  * `(type int32 (type uint32 123))`
  * `(type int32 (type float 123.321))` — Returns `123` (truncated)
  * `(type int32 (type float -123.321))` — Returns `-123` (truncated)

- Arrays

  * `(arr uint32 16 foo)` where 16 is an array size and `foo` is the name
  * `(arr-get foo 0)` to get an array element by index 0
  * `(arr-set foo 0 123)` to set an array element by index 0 to `123` value
  * `(arr-size foo)` to get `foo` array size (e.g. `16` of `uint32` type)

- There are no user-defined functions (at least for now)

- There are no conventional loops, only `(label foo)` definitions and
  `(goto foo)` jumps

- Supported instructions

  * Basic arithmetics
    + `(+ 2 3)` — Addition
    + `(- 2 3)` — Subtraction
    + `(* 2 3)` — Multiplication
    + `(/ 10 5)` — Floating-point division
    + `(div 10 4)` — Integer division
    + `(rem 10 4)` — Integer division remainder
    + `(mod 5 3)` — Modulo
    + `(floor 123.321 int32)` — Get a floor of a fractional number
    + `(ceiling 123.653 int32)` — Get a ceiling of a fractional number
    + `(round 123.653 int32)` — Get a rounded fractional number
    + `(trunc 123.653 int32)` — Get a truncated fractional number

  * Variables
    + `(var foo uint32 123)` — Define a variable (can be defined only once)
    + `(set foo (+ foo 3))` — Set a value to an already defined variable

  * If-conditions and comparisons:

    + `(if (> 5 2) (println bigger) (println less))`
      The condition expression is C-like
      (`0` is false and any other number is considered true)
    + `(true)` is an alias for `1`
    + `(false)` is an alias for `0`

    + Comparison functions/operators
      + `(< a b)` — Less
      + `(<= a b)` — Less or equal
      + `(> a b)` — More
      + `(>= a b)` — More or equal
      + `(== a b)` — Equal
      + `(!= a b)` — Not equal

  * Strings

    + Literals
      + A word matching `[a-zA-Z-_0-9]` regular expression
      + A string wrapped with double quotes (`"foo"`)
        where quotation marks inside can be escaped with a backslash `\"`

    + String concatenation
      + `(str foo bar)`
      + `(str "foo bar" baz)`
      + `(str foo (str bar baz))`

  * Extendable standard library

    + It’s easy to pre-populate the “standard library” (the definitions/symbols
      hash-table) with custom FFI bindings.

      Examples of already defined functions:

      + Printing to stdout
        + `(println "hello there")`
        + `(print hi)`
        + `(println (str hello there))`
      + Printing to stderr
        + `(eprintln "hello there")`
        + `(eprint hi)`
        + `(eprintln (str hello there))`

  * Early exit (by default last expression is returned from the program)

    + `(exit 0 123)` where `0` is exit status code and `123` is returned value
      (defaulting to `uint32` type)

- Comments

  Conventional LISP comments:

  * `; Arbitrary text until the end of a line`
  * `#| Arbitrary inline comment text |#`

- When you need to pass input values for the program you just pre-populate the
  symbols/defintions hash-table with values that can be read then inside the
  program
