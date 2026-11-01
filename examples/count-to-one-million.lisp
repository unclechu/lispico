(var "counter" 0)
(label "loop")

(if
  (and (> counter 0) (== (mod counter 100000) 0))
  (println (str "Counted to " (type s counter))))

(if
  (< counter 1000000)
  (do
    (set "counter" (+ counter 1))
    (goto loop)))

(assert (== counter 1000000) "Must count to one million")
