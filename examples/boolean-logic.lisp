; Basic conjunctions
(assert (== (and true true) true))
(assert (== (and true true true) true))
(assert (== (and true false) false))
(assert (== (and true false true) false))

; Basic disjunctions
(assert (== (or false true) true))
(assert (== (or false true false) true))
(assert (== (or false false) false))
(assert (== (or false false false) false))

; Zero and single arguments
(assert (== (and) true))
(assert (== (or) false))
(assert (== (and true) true))
(assert (== (and false) false))
(assert (== (or true) true))
(assert (== (or false) false))

; Numeric truthiness
(assert (== (and 123 -1 0.5) true))
(assert (== (or 0 0.0 42) true))
(assert (== (and 123 0) false))

; Short-circuiting skips invalid arguments
(assert (== (and false "hello") false))
(assert (== (or true "hello") true))

; Short-circuiting skips expressions with side effects
(assert (== (and false (exit 1 "unexpected")) false))
(assert (== (or true (exit 1 "unexpected")) true))

; Nested boolean expressions
(assert (and (or false true) (and true true)))
(assert (== (or (and true false) (and true true)) true))
