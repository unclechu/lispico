(var "counter" 0)
(label "loop")
(if (< counter 1000000) (do (set "counter" (+ counter 1)) (goto loop)))
(assert (== counter 1000000) "Must count to one million")
