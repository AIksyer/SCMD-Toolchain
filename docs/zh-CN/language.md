# 语言基础

## 文件

SCMD 源文件扩展名 `.scmd`，项目文件 `.scmdproj`，源码按 UTF-8 处理。

## 关键字

0.11 主要关键字：

```text
get const compile volatile
var bool u8
function export resident
if else while for return
true false wait
block record jump
```

旧版 `import / fn / func / bit` 暂作为兼容别名，新代码不应继续使用。

## `get`

```scmd
get "logic/adder.scmd";
get "ui/menu.scmd";
```

路径相对当前源文件。当前所有 `get` 进入同一 flat namespace；module namespace 尚未实现。

## 运行时变量

```scmd
bool enabled = true;
u8 count = 42;
var value = 10;
```

`u8` 为 0..255，运算按 8 位回绕。`var` 必须有初始化器。

## `const`

`const` 是编译期值，不分配运行时 alias storage：

```scmd
const INODES = 16;
const ENABLED = true;
```

0.11 的 const evaluator 支持 bool / integer expression；一等 compile-time `string` 尚未实现。

## 固定数组

```scmd
const N = 64;
u8 data[N] = 0;
bool used[16] = false;
```

当前限制：

- 仅 top-level `bool` / `u8`；
- 长度必须是 compile-time integer，范围 `1..256`；
- 初始化器当前是一个 uniform scalar，`compile {}` 可以继续逐元素改初值；
- 运行时动态索引类型必须是 `u8`；
- 动态 element write 当前只支持 `=`。

静态索引直接 lower 到目标 element；动态读取由后端生成选择网络，动态写入生成 index dispatch。

## `compile {}`

编译期操作使用与普通 SCMD **完全相同的控制流语法**：

```scmd
const N = 4;
u8 values[N] = 0;

compile
{
    for(var i = 0; i < N; i += 1)
    {
        values[i] = i + 1;
    }

    assert(values[3] == 4);
}
```

这段由 `scmdc` 执行，循环/赋值不会进入生成的 CFG。

0.11 compile block 支持 `var / bool / u8` locals、赋值、数组读写、`if / while / for` 和 `assert(expr)`。compile-local `var` 使用编译器整数语义，因此循环计数器不被 runtime `u8` 限制；显式 `u8` 仍检查 0..255。

为了保持构建确定性，compile block **没有 subprocess、shell escape、宿主文件写入或任意 host command**。普通 runtime function call 也尚未作为 compile-time function 开放。

## `volatile`

```scmd
volatile u8 host_state = 0;
```

`volatile` 表示该变量具有必须保留的可观察 storage identity。CS2 backend 不会对它做 storage-width narrowing，也不会把它当作可消失/可合并的普通临时状态。仍允许不破坏其读写语义的优化。

这和 C/C++ 一样**不是“整个函数禁止优化”**。

## `@noopt`

需要精确保留后端 alias graph 时：

```scmd
@noopt
function host_bridge()
{
    host_state = 1;
    host_state = 2;
}
```

`@noopt` function 内生成的 CFG alias 会被标为 optimizer-preserved/opaque，不做普通 forwarding、dedup、fusion 或内部 rename。

Top-level variable 也可使用 `@noopt`，用于要求完整 storage representation 的特殊状态。

函数属性还支持：

```scmd
@export
@resident
function tty_entry()
{
}
```

旧的 `export function` / `resident function` / `export resident function` 继续兼容。

## Unicode 标识符

```scmd
bool 启用 = true;
u8 玩家数量 = 3;

function 显示状态()
{
    console.print("ok");
}
```

用户标识符只存在于编译期，CS2 输出使用 ASCII mangled alias。

## 函数

```scmd
function hello()
{
    console.print("hello");
}
```

调用：`hello();`。

`export function` 暴露稳定 Console alias；普通函数仍使用 mandatory demand loading。`resident function` body 放在 eager core，适合 clear/showconsole 之后绝不能再触发 lazy `exec` 的时序敏感路径。

当前限制：函数参数、返回值表达式和递归调用栈尚未开放；递归调用会被 sema 拒绝。

## 条件与循环

```scmd
if(enabled)
{
}

while(count < 10)
{
    count += 1;
}

for(var i = 0; i < 10; i += 1)
{
    count += 1;
}
```

条件括号是语法的一部分。当前没有 `break / continue`。

## I/O

```scmd
console.clear();
console.print("hello");
chat.send("hello everyone");
teamchat.send("rush b");
command.exec("mp_restartgame 1");
```

`console.print` 在当前 CS2 backend lower 到 `echoln`，避免真实 CS2 的 Source `echo` `[Console]` 前缀。

## 时间

```scmd
wait 16ms;
wait 1s;
wait 3ticks;
```

`tick` 使用项目配置的 SCMD scheduler tick，不等同服务器 tickrate。
