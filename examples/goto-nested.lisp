; A label declared inside (do) remains globally visible.
(do
  (var "counter" 0)

  (label "loop")

  (set "counter" (+ counter 1))

  (if (< counter 5)
    (goto loop)
    counter))

counter
