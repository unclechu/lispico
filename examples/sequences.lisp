(do
  (+ 2 3)
  (do
    (+ (last-val) 4)
    (+ (last-val) 5)))
(+ (last-val) 10) ; TYPE_U32(24)
