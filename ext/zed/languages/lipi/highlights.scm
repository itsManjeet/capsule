; Comments and literals.
(comment) @comment
(block_comment) @comment
(string) @string
(escape_sequence) @string.escape
(number) @number
(boolean) @boolean

; Reader syntax.
(quote) @punctuation.special
(quasiquote) @punctuation.special
(unquote) @punctuation.special
(unquote_splicing) @punctuation.special

; Forms whose first symbol is callable.
(list (symbol) @function)

; Language forms and directives override normal call highlighting.
((symbol) @keyword
 (#match? @keyword "^(define|define-macro|let|if|begin|cond|else|set|quote|quasiquote|unquote|unquote-splicing|for-each)$"))

((symbol) @keyword.directive
 (#match? @keyword.directive "^#(import|extern|inline)$"))

; Core macros and standard-library control forms.
((symbol) @keyword
 (#match? @keyword "^(do|when|unless|and|or|not)$"))

; Builtins and primitive operators.
((symbol) @operator
 (#match? @operator "^(\\+|-|\\*|/|=|<|>|<=|>=)$"))

((symbol) @function.builtin
 (#match? @function.builtin "^(len|index|append|assert|assert-eq|gensym|list|cons|car|cdr|null\\?|list\\?|symbol\\?)$"))

; Type names and typed declarations.
((symbol) @type
 (#match? @type "^(i8|i16|i32|i64|u8|u16|u32|u64|bool|ptr|none|void|str|array|list|map)$"))

((symbol) @type
 (#match? @type ":[A-Za-z0-9_-]+$"))

; Inline assembly/C profile names in (#inline asm ```...```).
((symbol) @constant
 (#match? @constant "^(asm|asm-global|c)$"))
