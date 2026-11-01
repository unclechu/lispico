; Boolean `true` and `false` values are just u32 variables
(assert (and (== true 1) (== (type-of true) u32)))
(assert (and (== false 0) (== (type-of false) u32)))

(assert (== (< 2 3) true)) ; true: 1
(assert (== (>= 3.0 3.0) true)) ; true: 1
(assert (== (== "hello" "hello") true)) ; true: 1
(assert (== (< "apple" "banana") true)) ; bytewise lexical order: true
(assert (== (!= "foo" "bar") true)) ; true: 1
