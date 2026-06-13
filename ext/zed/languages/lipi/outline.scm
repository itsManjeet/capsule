((list
  (symbol) @_define
  (list
    (symbol) @name)) @item
 (#eq? @_define "define"))

((list
  (symbol) @_define_macro
  (list
    (symbol) @name)) @item
 (#eq? @_define_macro "define-macro"))

((list
  (symbol) @_define
  (symbol) @name) @item
 (#eq? @_define "define"))
