(var "counter" 0)

(while (< counter 1_000_000) (do
  (if
    (and (> counter 0) (== (mod counter 100_000) 0))
    (println (str "Counted to " (type s counter))))

  (set "counter" (+ counter 1))
))

(assert (== counter 1_000_000) "Must count to one million")
