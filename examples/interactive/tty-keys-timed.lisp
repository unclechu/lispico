; Interactive desktop example: run in a real terminal, not with piped stdin.
; Controls: W/A/S/D, Q to quit. TTY input works without pressing Enter.
;
; Example: ./lispico -ps examples/interactive/tty-keys.lisp

(set-stdout-buffering 0)

(arr-new "terminal-size" u32 3)
(arr-set "terminal-size" 2 123)
(tty-size "terminal-size")

(println (str
  "Terminal size: "
  (type s (arr-get terminal-size 0))
  " columns by "
  (type s (arr-get terminal-size 1))
  " rows"))
(assert (== (arr-get terminal-size 2) 123))

(println "Press W/A/S/D to move, Q to quit.")
(tty-enable)
(do
  (defer (tty-disable))

  (var "running" true)
  (var "key" 0)
  (while running
    (do
      ; A timed read updates key only on success and returns a boolean.
      (if (tty-read-byte "key" 100)
        (do
          (if (== key 119) (println "Up"))
          (if (== key 97) (println "Left"))
          (if (== key 115) (println "Down"))
          (if (== key 100) (println "Right"))
          (if (== key 113) (set "running" false)))))))

(println "TTY settings restored.")
