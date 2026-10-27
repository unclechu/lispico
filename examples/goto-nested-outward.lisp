(var "counter" 0)

(label "outer")

(do
  (set "counter" (+ counter 1))
  (if (< counter 3)
    (goto outer)))

(exit 0 counter)

; Evaluation result is 3, the final counter value
