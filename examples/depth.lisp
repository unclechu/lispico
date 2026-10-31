; Top-level depth
(assert (== (depth) 0))

; Nested sequences
(do
  (assert (== (depth) 1))

  (do
    (assert (== (depth) 2))

    (do
      (assert (== (depth) 3)))

    (assert (== (depth) 2)))

  (assert (== (depth) 1)))

(assert (== (depth) 0))

; Other special forms don't introduce new sequence levels
(assert (== (if true (depth) 123) 0))
(assert (and true (== (depth) 0)))
(assert (or false (== (depth) 0)))

; Check depth after jumping into a previously completed nested sequence
(var "counter" 0)

(do
  (do
    (label "deep")
    (assert (== (depth) 2))
    (set "counter" (+ counter 1))))

(assert (== (depth) 0))

(if (< counter 2) (goto deep))

(assert (== counter 2))
(assert (== (depth) 0))
