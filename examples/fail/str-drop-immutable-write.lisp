(var "part" (str-drop "Hello" 1))
(set "part" "ello") ; Cannot write through a read-only view
