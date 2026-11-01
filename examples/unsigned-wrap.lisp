; Unsigned arithmetic wraps modulo 2^32.
(assert (== (- 2 3) 4294967295))
(assert (== (type s (- 2 3)) "4294967295"))
