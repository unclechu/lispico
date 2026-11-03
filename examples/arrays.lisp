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
