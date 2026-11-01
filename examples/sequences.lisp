(do
  (+ 2 3)
  (do
    (+ (last-val) 4)
    (+ (last-val) 5)))
(+ (last-val) 10)
(assert (== (last-val) 24) "Final value must be 24")
