# 语言基础

## 文件

SCMD 源文件扩展名：

```text
.scmd
```

项目文件：

```text
.scmdproj
```

源码按 UTF-8 处理。

## 关键字

0.6 当前主要关键字：

```text
get var bool u8 function
if else while for return
true false wait
block record jump
```

旧版 `import` / `fn` / `func` / `bit` 暂时作为兼容别名接受，但新代码不应继续使用。

## `get`

```scmd
get "logic/adder.scmd";
get "ui/menu.scmd";
```

路径相对于当前源文件解析。编译器递归构建依赖图，同一文件不会重复加载。

当前所有 `get` 文件进入同一 flat namespace；模块命名空间尚未实现。

## 变量

显式类型：

```scmd
bool enabled = true;
u8 count = 42;
```

类型推导：

```scmd
var flag = false;
var value = 10;
```

`var` 必须带初始化器。

当前基础类型：

- `bool`
- `u8`（0..255，运算按 8 位回绕）

## Unicode 标识符

以下均合法：

```scmd
bool 启用 = true;
u8 玩家数量 = 3;

function 显示状态()
{
    console.print("ok");
}
```

用户标识符只存在于编译期。CS2 输出使用 ASCII mangled alias，因此中文名字不会直接成为 Console command 名。

## 函数

```scmd
function hello()
{
    console.print("hello");
}
```

调用：

```scmd
hello();
```

0.6 当前限制：

- 函数参数语法尚未开放。
- 返回值尚未开放；只支持 `return;`。
- 递归调用会被语义分析拒绝。

## 条件

条件括号是语法的一部分：

```scmd
if(enabled)
{
}
```

以下非法：

```scmd
if enabled
{
}
```

大括号是否换行不是语义要求，因此以下也合法：

```scmd
if(enabled){
    console.print("legal");
}
```

项目代码推荐：

```scmd
if(enabled)
{
    console.print("preferred");
}
```

未来 formatter 会统一为项目推荐格式，而不是让 Parser 因排版拒绝代码。

## 循环

```scmd
while(count < 10)
{
    count += 1;
}
```

```scmd
for(var i = 0; i < 10; i += 1)
{
    count += 1;
}
```

当前没有 `break` / `continue`。

## I/O

Console：

```scmd
console.clear();
console.print("hello");
```

聊天：

```scmd
chat.send("hello everyone");
teamchat.send("rush b");
```

原始 CS 命令：

```scmd
command.exec("mp_restartgame 1");
```

`command.exec` 是逃生口；应优先使用有语义的 SCMD API。

## 时间

```scmd
wait 16ms;
wait 1s;
wait 3ticks;
```

`tick` 在 CS2 backend 中由项目的 `console.tick` 毫秒值换算。它是 SCMD scheduler tick，不等同于服务器 tickrate。
