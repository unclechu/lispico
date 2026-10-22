# Simple LISP-ish Interpreter Written in C

**WORK IN PROGRESS: Not ready to use.**

This is not really a conventional LISP interpreter, only relevant in terms of
syntax (for the sake of parser implementation simplicity). It’s more like a
simple low-level calculation DSL language.

This allows DSL-like computations to be parsed and evaluated dynamically in
embedded platforms such as Raspberry Pico as part of a firmware for it written
in C.

## Features

To keep the implementation simple, the language supports only a limited set of
features:

- Global state stored in a single static hash-table.

  There are no lexical name scopes. If you need some level of name scoping you
  can use name prefixes for example.

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
    + `(- 3 2)` — Subtraction
      (you might want to convert the values to `int32` explicitly, since in this
      case the default result type is `uint32` since the input arguments are
      defaulting to `uint32`, so negative number result will end up with number
      underflow)
    + `(* 2 3)` — Multiplication
    + `(/ 10.0 5.0)` — Floating-point division
      (attempt to divide by zero results into a failures;
      be aware that `(/ 10 5)` will result into default `uint32` type,
      so it would behave like `(div)` and you will loose the remainder)
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

    - `(/ 7.5 2.0)` — Result is `3.75` (`float`)
    - `(div 7.5 2.0)` — Result is `3.0` (`float`)
    - `(/ 7 2)` — Result is `3` (`uint32`)
    - `(div 7 2)` — Result is `3` (`uint32`)
    - `(div -7.5 2.0)` — Result is `-3.0` (`float`)
    - `(div 10 4)` — Division with the fractional part of the
      quotient truncated toward zero. Preserves the operand type.
    - `(div -7 2)` — Failure (`int32` vs. `uint32` type mismatch).
      Use explicit type conversion (`(div -7 (type int32 2))`)
    - `(mod 5.5 2.0)` — Result is `1.5`
      (C `fmodf` behavior for `float`;
      for `uint32` and `int32` C `%` is used instead)
    - `(- 2 3)` — Result is `UINT32_MAX`/`4294967295`
      (defaulting to `uint32`, number underflow, you might want to explicitly
      convert the values to `int32` first when expecting negative value as a
      result of the expression `(- (type int32 2) (type int32 3))` to get
      `int32` resulting type)

    Note that number overflow/underflow is preserving C behavior:

    - `uint32_t` overflow/underflow — Well-defined wrapping modulo (`pow(2, 32)`)
    - `int32_t` overflow — Undefined behavior
      (e.g. `(/ (type int32 -2147483648) (type int32 -1))`)
    - `float` overflow — Depends on the floating-point implementation;
      commonly produces infinity (`INFINITY` from `math.h`)
    - `INT32_MIN / -1` — Undefined behavior on 32-bit signed integers
      Out-of-range `float` → `int32_t` conversion

    Exceptions:

    - In C integer division by zero is undefined behavior, but this interpreter
      checks it and fails on the attempt

  * Variables
    + `(var foo uint32 123)` — Define a variable (can be defined only once)
    + `(set foo (+ foo 3))` — Set a value to an already defined variable

  * If-conditions and comparisons:

    + `(if (> 5 2) (println "bigger") (println "less"))`
      The condition expression is C-like
      (`0` is false and any other number is considered true)
    + `(true)` is an alias for `1`
    + `(false)` is an alias for `0`

    + Comparison functions/operators

      Note that the result of this functions is always of `uint32` type.
      The rule for the arguments is the same. All arguments must be of the same
      type. Otherwise the interpreter will refuse to evaluate and fail.

      + `(< a b)` — Less
      + `(<= a b)` — Less or equal
      + `(> a b)` — More
      + `(>= a b)` — More or equal
      + `(== a b)` — Equal
      + `(!= a b)` — Not equal

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

  * Early exit (by default last expression is returned from the program)

    + `(exit 0 123)` where `0` is exit status code and `123` is returned value
      (defaulting to `uint32` type)

- Comments

  Conventional LISP comments:

  * `; Arbitrary text until the end of a line`
  * `#| Arbitrary inline comment text |#`

- When you need to pass input values for the program you just pre-populate the
  symbols/definitions hash-table with values that can be read then inside the
  program

## License

SPDX: MIT

All files in this repository (apart from third-party dependencies) are
licensed under [MIT](LICENSE).
