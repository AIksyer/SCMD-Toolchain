# 语法草案（0.11）

下面描述当前 Parser 的主要结构，不是完整形式化规格。

```text
unit        := { attributes? (get | const | compile | global | function | export_function | resident_function | block) }
attributes  := { '@' ('noopt' | 'export' | 'resident') }
get         := 'get' STRING ';'
const       := 'const' IDENT '=' expr ';'
compile     := 'compile' brace_block
global      := [ 'volatile' ] ('var' | 'bool' | 'u8') IDENT [ '[' expr ']' ] '=' expr ';'
function    := 'function' IDENT '(' ')' brace_block
export_function   := 'export' [ 'resident' ] 'function' ASCII_IDENT '(' ')' brace_block
resident_function := 'resident' 'function' IDENT '(' ')' brace_block
block       := 'block' IDENT '</' { statement } '/>'
brace_block := '{' { statement } '}'

if          := 'if' '(' expr ')' brace_block [ 'else' (if | brace_block) ]
while       := 'while' '(' expr ')' brace_block
for         := 'for' '(' [for_part] ';' [expr] ';' [for_part] ')' brace_block

array_read  := IDENT '[' expr ']'
array_write := IDENT '[' expr ']' '=' expr ';'
record      := 'record' IDENT ';'
jump        := 'jump' IDENT ';'
wait        := 'wait' NUMBER ('ms'|'s'|'tick'|'ticks') ';'
```

## 编译期控制流

`compile {}` **复用普通 SCMD statement grammar**：

```scmd
compile
{
    for(var i = 0; i < 16; i += 1)
    {
        if(i < 8)
        {
            table[i] = i + 1;
        }
    }
}
```

没有 `0..16`、`for(i in ...)` 或第二套 comptime 循环语法。

当前 compile interpreter 接受：

- `var / bool / u8` compile-local；
- 普通赋值和复合赋值；
- `if / while / for`；
- global fixed-array 读写；
- `assert(expr);`。

普通函数调用、I/O、`wait`、Source command、Block 等不能在 `compile {}` 内执行。

## 固定数组

0.11 fixed array 只能是 top-level `bool` / `u8`，长度必须是编译期整数，当前范围 `1..256`：

```scmd
const N = 64;
u8 buffer[N] = 0;
bool used[16] = false;
```

运行时动态索引必须为 `u8`。动态 element write 当前只支持 `=`；`array[i] += 1` 会明确报错。

## 优化属性

```scmd
volatile u8 host_state = 0;

@noopt
function bridge()
{
}

@export
@resident
function tty_entry()
{
}
```

`@export` / `@resident` 的 attribute 写法与旧 modifier 语法兼容。

控制流括号 `if(...) / while(...) / for(...)` 仍然是语法强制要求；大括号换行不是语义要求。
