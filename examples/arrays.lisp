; Fixed-size, zero-initialized u32 arrays
(arr-new "numbers" u32 16)
(assert (== (arr-size "numbers") 16))
(assert (== (arr-type "numbers") u32))
(assert (== (arr-get "numbers" 0) 0))
(assert (== (arr-get "numbers" 15) 0))
(assert (== (type-of (arr-get "numbers" 0)) u32))
(arr-set "numbers" 0 123)
(arr-set "numbers" 15 (+ 1 2 3))
(assert (== (arr-get "numbers" 0) 123))
(assert (== (arr-get "numbers" 15) 6))
(assert (== (arr-size "numbers") 16))

; Computed index and length
(arr-new "computed" u32 (+ 2 3))
(arr-set "computed" (- 5 1) 42)
(assert (== (arr-get "computed" 4) 42))

; Signed integers
(arr-new "signed" i32 2)
(assert (== (arr-type "signed") i32))
(assert (== (arr-get "signed" 0) (type i32 0)))
(arr-set "signed" 1 -123)
(assert (== (arr-get "signed" 1) -123))
(assert (== (type-of (arr-get "signed" 1)) i32))

; Floating-point elements
(arr-new "floats" f32 2)
(assert (== (arr-type "floats") f32))
(assert (== (arr-get "floats" 0) 0.0))
(arr-set "floats" 1 1.5)
(assert (== (arr-get "floats" 1) 1.5))
(assert (== (type-of (arr-get "floats" 1)) f32))

; String elements are initially empty and are copied independently
(arr-new "words" s 3)
(assert (== (arr-type "words") s))
(assert (== (str-size (arr-get "words" 0)) 0))
(var "message" "Hello")
(arr-set "words" 0 message)
(arr-set "words" 1 "abc\0def")
(str-set "message" "Hi")
(assert (== (arr-get "words" 0) "Hello"))
(assert (== (str-size (arr-get "words" 1)) 7))
(assert (== (str-char (arr-get "words" 1) 3) 0))
(arr-set "words" 0 "Bye")
(assert (== (arr-get "words" 0) "Bye"))
(assert (== (str-size (arr-get "words" 2)) 0))

; Zero-length arrays are allowed
(arr-new "empty" u32 0)
(assert (== (arr-size "empty") 0))
(assert (== (arr-type "empty") u32))

; The array remains a distinct kind, not a scalar variable
(assert (== (kind-of "numbers") arr-kind))

; Read array elements directly from evaluated array values
(arr-new "direct" u32 3)
(arr-set "direct" 0 123)
(arr-set "direct" 1 456)

; Array name and array value are both accepted
(assert (== (arr-get "direct" 0) 123))
(assert (== (arr-get direct 0) 123))
(assert (== (arr-get direct 1) 456))
(assert (== (arr-get direct 2) 0))

; Read from the result of (arr-new)
(assert (== (arr-get (arr-new "created" u32 2) 0) 0))

; Read from the result of (arr-set)
(assert (== (arr-get (arr-set "direct" 2 789) 2) 789))

; String array values
(arr-new "direct-strings" s 2)
(arr-set "direct-strings" 0 "Hello")
(assert (== (arr-get direct-strings 0) "Hello"))
(assert (== (arr-get direct-strings 1) ""))

; Array size accepts both names and evaluated array values
(arr-new "numbers-2" u32 3)
(assert (== (arr-size "numbers-2") 3))
(assert (== (arr-size numbers-2) 3))

; Array element type accepts both names and evaluated array values
(assert (== (arr-type "numbers-2") u32))
(assert (== (arr-type numbers-2) u32))

; Directly use results returned by array functions
(assert (== (arr-size (arr-new "created-2" i32 5)) 5))
(assert (== (arr-type (arr-new "created-strings" s 2)) s))
(assert (== (arr-size (arr-set "numbers-2" 0 123)) 3))
(assert (== (arr-type (arr-set "numbers-2" 1 456)) u32))

; Empty arrays are valid
(arr-new "empty-2" f32 0)
(assert (== (arr-size empty-2) 0))
(assert (== (arr-type empty-2) f32))

; The result types remain u32
(assert (== (type-of (arr-size numbers-2)) u32))
(assert (== (type-of (arr-type numbers-2)) u32))
