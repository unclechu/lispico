; Jump back into a (do) block after it has finished.

(var "counter" 0)

(do
  (label "inner")
  (set "counter" (+ counter 1))
  (if (== counter 3)
    (exit 0 counter)))

(goto inner)

; Evaluation result is 3, the final counter value
