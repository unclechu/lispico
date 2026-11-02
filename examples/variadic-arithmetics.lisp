; Addition
(assert (== (+ 1 2) 3))
(assert (== (+ 1 2 3) 6))
(assert (== (+ 1 2 3 4 5) 15))
(assert (== (+ u32-max 1) 0))
(assert (== (+ u32-max 1 2) 2))

; Subtraction (left to right)
(assert (== (- 20 3 2) 15))
(assert (== (- 100 10 5 2) 83))
(assert (== (- 2 3 4) 4_294_967_291))

; Multiplication
(assert (== (* 2 3) 6))
(assert (== (* 2 3 4) 24))
(assert (== (* 1 2 3 4 5) 120))
(assert (== (* u32-max 2 3) 4_294_967_290))

; Division (left to right)
(assert (== (/ 100 5 2) 10))
(assert (== (/ 100 2 5 2) 5))
(assert (== (/ 7 2 1) 3))

; Signed integer arithmetic
(assert (== (+ -1 (type i32 2) (type i32 3)) (type i32 4)))
(assert (== (- -10 (type i32 5) (type i32 3)) -18))
(assert (== (* -2 (type i32 3) (type i32 4)) -24))
(assert (== (/ -100 (type i32 2) (type i32 5)) -10))
(assert (== (div -100 (type i32 2) (type i32 5)) -10))

; Floating-point arithmetic
(assert (== (+ 1.0 2.0 3.0) 6.0))
(assert (== (- 10.0 2.5 1.5) 6.0))
(assert (== (* 2.0 1.5 4.0) 12.0))
(assert (== (/ 7.5 2.0 0.5) 7.5))

; (div) truncates after each division
(assert (== (div 9.0 2.0 0.5) 8.0))
(assert (== (div 100 3 2) 16))
(assert (== (div -9.0 2.0 0.5) -8.0))

; Nested expressions still work
(assert (== (+ 1 2 (* 3 4)) 15))
(assert (== (* (+ 1 2 3) (- 10 5)) 30))

; Return types are preserved
(assert (== (type-of (+ 1 2 3)) u32))
(assert (== (type-of (- -10 (type i32 2) (type i32 3))) i32))
(assert (== (type-of (* 1.0 2.0 3.0)) f32))
(assert (== (type-of (/ 100 5 2)) u32))
(assert (== (type-of (div 9.0 2.0 0.5)) f32))

; (mod) remains binary
(assert (== (mod 10 3) 1))
