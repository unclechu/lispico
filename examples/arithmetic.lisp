; Binary operators preserve the operands' type.
(+ 2 3)
(- 3 2)
(* 6 7)
(/ 7 2) ; Integer division: 3 (u32)
(/ 7.5 2.0) ; Floating-point division: 3.75
(div 7.5 2.0) ; Truncate the quotient but preserve f32: 3.0
(mod 5.5 2.0) ; Floating-point remainder: 1.5
(/ 7.5 2.0) ; Final result: TYPE_F32(3.750000)
