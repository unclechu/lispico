; Only the selected branch runs.

(if (> 5 2)
  (println "bigger")
  (println "less"))

(assert
  (==
    (if false (+ 100 200) (+ 2 3))
    (type u32 5)))
