# SCB 字节码与寄存器 VM

SCMD 0.9 引入 **SCB（SCMD Bytecode）ABI 1**。

它的主要目标不是替代 CS2 CFG 输出，而是给 `scmdsim` 一个：

- 可验证；
- 可预编译；
- 不需要运行时反复解析 CFG 文本；
- 保持 CS2 Console lowered semantics；
- 对大型 alias/state-machine workload 更快的执行层。

## 数据流

0.11.1 仍保留同一个 canonical 路径：

```text
.scmd
  │
  ▼
scmdc frontend / backend
  │
  ▼
CFG package ───────────────► CS2
  │
  │ CFG -> SCB compiler
  ▼
SCB v1
  │
  ▼
scmdsim 16-register VM
```

这是刻意的 correctness 选择。

例如 SCMD 的 `u8 + u8` 在 CFG backend 中可能已经 lowering 成 ripple/state-machine alias 网络。SCB 编译的是这个最终 CFG 语义，而不是在 simulator 中偷偷变成宿主 C++ 的 `a + b`。因此：

```text
真实 CS2 backend 有 bug
```

不会因为 simulator “走捷径”而被隐藏。

未来可以把 CFG emitter 与 SCB emitter 都接到同一套更正式的 lowered IR；0.11.1 尚未进行这次 codegen IR 重构。

## `scmdc pack`

```powershell
scmdc pack build -o program.scb
```

可以指定当前正式 profile：

```powershell
scmdc pack build -o program.scb --profile cs2-2026
```

`pack` 会递归读取 root 下所有 `.cfg`，每个 CFG 成为一个可被 `exec` 定位的 module。

## `scmdsim`

运行 CFG root：

```powershell
scmdsim build
```

0.11.1 默认使用 lazy module compile：

```text
enter REPL immediately
  ↓
exec foo
  ↓
read only foo.cfg
  ↓
compile module to SCB representation
  ↓
in-process cache + register VM
```

修改已经执行过的 CFG 后，下一次 `exec` 会按 size/mtime 检测并重新编译；启动后新放入的 CFG 也能直接执行，并会出现在 Tab 路径补全中。

跨进程可选持久缓存：

```powershell
scmdsim build --cache
```

显式完整 AOT：

```powershell
scmdsim build --precompile
scmdsim build --save-scb build.scb
```

之后：

```powershell
scmdsim build.scb
```

`.scb` 是不可变 package snapshot，不再观察磁盘 CFG。对于固定发布物 / benchmark / 反复运行超大 package，推荐预编译 `.scb`。

## SCB v1 格式

Header 当前记录：

```text
magic              SCB1
ABI version        1
header size
flags
profile string id
string count
block count
instruction count
payload byte count
payload checksum
```

Payload 包含：

```text
string pool
block/module table
instruction stream
```

checksum 当前为 64-bit FNV-1a。它用于检测破损/意外修改，不是密码学签名。

SCB ABI 与工具版本独立：

```text
SCMD toolchain 0.11.1
SCB ABI 1
```

未来后续版本仍可继续读取 ABI 1，只要格式没有 incompatible change。

## Verifier

加载 `.scb` 时在执行前验证：

- magic；
- ABI / header size；
- payload length；
- checksum；
- count hard limit；
- block instruction range；
- opcode range；
- VM register index / register window；
- string id；
- block id；
- module table target。

损坏 package 应得到明确错误，而不是跳到任意 PC 或越界读取。

## Register VM

SCB VM 是**寄存器机**。

```text
R0 .. R15
```

每个 stream 拥有自己的 16 个 64-bit VM register。

这里的寄存器只是 simulator bytecode execution registers，和 vCS-16 guest CPU 的 `r0..r7` 完全不是同一层。

```text
SCB register VM
      ↓
CFG / alias semantics
      ↓
vCS-16 guest CPU
```

因此在 vCS 实验中确实可能出现“寄存器机里模拟寄存器机”。

## Predecoded instruction

磁盘 SCB 是二进制格式；加载后 instruction 直接进入固定 16-byte decoded representation：

```cpp
struct Instruction
{
    uint8_t op;
    uint8_t dst;
    uint8_t a;
    uint8_t b;
    uint32_t x;
    uint32_t y;
    uint32_t z;
};
```

热循环不需要每次重新 tokenize CFG、解析字符串或 decode 变长 operand。

## Fused / superinstructions

普通 register op 仍然存在，但大量 CS2 Console 指令参数在编译时就是常量。

因此当前 VM 会把常见形式压成 fused op，例如：

```text
ALIAS_SET_IMMEDIATE
EXEC_IMMEDIATE
EXEC_ASYNC_IMMEDIATE
ECHOLN_IMMEDIATE
SETINFO_IMMEDIATE
DISPATCH0
DISPATCH1
```

避免把：

```text
LOADK R0, "hello"
ECHOLN R0
```

机械地执行成两拍。

这不改变 Console 语义，只减少 VM dispatch 数。

## Alias

Alias body 在 CFG→SCB compile 时变为 bytecode block：

```cfg
alias state "foo"
```

概念上：

```text
alias["state"] -> BlockID
```

重新定义 alias 只需要把映射改到另一个 block，因此 SCMD 原本依赖的 mutable alias / continuation 模式保留。

## `exec`

CFG compile 时建立 module table：

```text
Scmd/Menu/Main.cfg
      ↓
module "Scmd/Menu/Main"
      ↓
BlockID
```

执行 `exec Scmd/Menu/Main` 时不再 `open()` 文件，而是直接 resolve module 并 push block。

支持：

- `/` / `\` 归一化；
- `.cfg` 后缀可省略；
- ASCII case-insensitive lookup；
- 拒绝绝对路径；
- 拒绝 drive / colon；
- 拒绝 `..` traversal。

## Async / sleep

每个 async stream 有：

```text
registers
block stack
ready time
stream id
```

`sleep N` 不会让宿主线程真的等待；它只把 stream 的虚拟 deadline 调到 `now + N`，scheduler 直接跳到下一 ready event。

所以模拟 `wait 60s` 可以瞬间结束，但虚拟时间会前进 60 秒。

## CFG source view 与 SCB snapshot

0.11.1 区分两种输入模型。

CFG root：

```text
lazy source view
```

- 启动时不全量 AOT；
- 第一次 `exec` 编译目标 module；
- 当前进程内复用已编译 module；
- size/mtime 变化后下一次 `exec` 热重载；
- 新增 CFG 可以直接执行 / Tab 补全；
- `--cache` 可跨进程复用 per-module SCB cache。

预编译 `.scb`：

```text
immutable package snapshot
```

- load / verify 后只使用 package module table；
- 不观察磁盘 CFG；
- 适合发布、基准测试和需要固定输入的回归。

这使开发模式更接近真实 CS2 `exec` 的动态文件行为，同时保留 SCB 快照的确定性。

## 当前性能边界

SCB v1 主要优化：

- runtime text parse；
- repeated exec filesystem work；
- alias body reparse；
- common command dispatch。

当前还没有重点优化：

- 超大 string pool 磁盘压缩；
- packed variable-width disk instruction；
- string dedup/reverse index 的极限内存；
- block compaction / DCE。

因此极端 generated package 的 `.scb` **可能比原始 CFG text 更大、peak RSS 也更高**。这不是 ABI 承诺；后续可以在不改变 VM 语义的前提下优化格式。
