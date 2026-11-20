(fn "a" (b))
(fn "b" (c))
(fn "c" (leave 3 42))

(assert (== (a) 42))
