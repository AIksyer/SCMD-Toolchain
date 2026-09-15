# 0.11 当前实现状态

## 语言已实现

- `get` 多文件 / flat namespace
- `bool` / `u8` / `var`
- `const` compile-time bool/integer values
- global fixed `bool[]` / `u8[]`（1..256）
- runtime array read / dynamic-index write (`=`)
- `compile {}`：普通 `if / while / for`、compile locals、array initialization、`assert`
- `volatile` runtime storage
- `@noopt`、`@export`、`@resident` top-level attributes
- UTF-8 / 中文标识符
- `function` / `export function` / `resident function`
- `if / else`、`while`、`for`、`return;`
- u8 算术/比较/位运算/复合赋值
- Block / record / jump / Block.run
- Console/chat/raw command APIs
- `wait`
- `.scmdproj`
- command-buffer-safe paging + mandatory demand loading

## 0.11 编译期约束

- compile phase 操作 AST/value，不是 textual macro；
- compile control flow 与 runtime SCMD 使用同一语法；
- compile-time function call 目前只开放 `assert(expr)`；
- 一等 compile-time string/array literal 尚未实现；
- 没有 subprocess/shell/任意 host file-write；
- fixed arrays 当前仅全局 `bool/u8`，长度 1..256；
- dynamic array compound write 暂未实现。

## 工具链已实现

- `scmdc` C17 frontend/backend + compile interpreter
- `scmdsim` C++20 Console/SCB simulator
- SCB ABI 1
- `libvcs16` / `libvcs16_scmd`
- `vcs16as` / `vcs16run` / `vcs16dump` / `vcs16scmd`
- CFG root lazy source view + persistent module cache
- deterministic async virtual scheduler
- SCB pack / verifier / fixed-width VM
- real-CS2 compatibility rules：literal `|`、builtin precedence、InputService exec messages、echo/echoln UI distinction

## 暂未实现

语言：

- 函数参数 / 返回值表达式
- 递归调用栈
- compile-time user functions / compile-time strings
- local fixed arrays / multidimensional arrays
- `break / continue`
- module namespace
- formatter / LSP
- stable diagnostic IDs
- Block.runUntil / runRange / nested record

字节码 / simulator：

- 正式 lowered IR 同时直出 CFG/SCB
- SCB 压缩/变长编码
- decompiler
- 完整 Source 2 command set

## CFG-root / SCB

`scmdsim cfg-root` 是开发期 lazy source view；`.scb` 是不可变 package snapshot。真实 CS2 仍是 Console compatibility 的最终权威。
