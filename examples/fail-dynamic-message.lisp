(var "limit" 4)
(var "input" "Hello")
(if (> (str-size input) limit)
  (fail (str "Input exceeds " (type s limit) " bytes")))
