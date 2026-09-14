# 语法草案（0.6）

下面不是完整形式化规格，只描述当前 Parser 的主要结构。

```text
unit        := { get | global | function | block }
get         := 'get' STRING ';'
global      := ('var' | 'bool' | 'u8') IDENT '=' expr ';'
function    := 'function' IDENT '(' ')' brace_block
block       := 'block' IDENT '</' { statement } '/>'
brace_block := '{' { statement } '}'

if          := 'if' '(' expr ')' brace_block [ 'else' (if | brace_block) ]
while       := 'while' '(' expr ')' brace_block
for         := 'for' '(' [for_part] ';' [expr] ';' [for_part] ')' brace_block

record      := 'record' IDENT ';'
jump        := 'jump' IDENT ';'
wait        := 'wait' NUMBER ('ms'|'s'|'tick'|'ticks') ';'
```

控制流括号：

```text
if(...)
while(...)
for(...)
```

是语法强制要求。

`{` 是否和 header 同行不是语法要求。
