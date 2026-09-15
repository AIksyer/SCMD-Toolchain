# 编译器、构建与诊断

## 编译器

`scmdc` 编译器主体使用 C17；0.7 起 Console 模拟器使用 C++20；0.9 的 SCB compiler / register VM 也使用 C++20。推荐 Clang + CMake + Ninja。

初始化编译器仓库：

```powershell
.\scripts\init.ps1
```

Debug 构建：

```powershell
.\scripts\build.ps1 -Config Debug
```

Release：

```powershell
.\scripts\build.ps1
```

清理：

```powershell
.\scripts\build.ps1 -Clean
```

## 单文件编译

```text
scmdc input.scmd -o output.cfg
```

常用参数：

```text
--console-mode sync|async
--console-settle-ms N
--tick-ms N
--page-bytes N
--page-commands N
--exec-prefix PATH
--no-opt
```

项目构建更推荐把这些目标参数写进 `.scmdproj`。

CS2 CFG 后端默认启用优化：会在保持 alias 动态行为、循环、`wait`/`exec` 和分页边界安全的前提下，删除不可达内部节点、合并安全的连续跳转、去重静态节点并缩短编译器生成的符号。需要保留原始 CFG 进行对照或调试时，可使用：

```text
scmdc input.scmd -o output.cfg --no-opt
scmdc build project.scmdproj --no-opt
```

## 诊断

SCMD 诊断会输出源码位置、原行和 caret：

```text
error: expected '(' after if, got identifier
 --> src/main.scmd:4:8
 4 |     if x
   |        ^
```

目标是保持 Kinal / Clang 风格的可读诊断，而不是只打印 `syntax error`。

0.9 尚未冻结稳定错误码体系；后续会加入 `SCMD-...` 诊断编号。

## 格式

编译器不会因为 `{` 是否换行而改变语义。

以下都能编译：

```scmd
if(x){
}
```

以及：

```scmd
if(x)
{
}
```

项目推荐后一种 Allman 风格。未来 `scmdfmt` 会负责格式统一。

## 编译器与模拟器分离

0.11.1 中 `scmdc` 负责语言编译、编译期执行、项目构建以及 CFG → SCB pack：

```text
scmdc build project.scmdproj
scmdc pack build -o project.scb
```

运行 lazy CFG source view 或最终 SCB package 使用独立工具：

```text
scmdsim build
# 或
scmdsim project.scb

> exec project
```

这样编译器与运行环境可以分别测试、发布和分析，同时 simulator core 只有一份实现。详见 [`scmdsim` Console 模拟器](simulator.md)。

## 0.11：frontend compile phase

0.11 在 parse/load 后、semantic analysis/codegen 前增加确定性的 compile-time phase：

```text
parse / load
    ↓
const resolution
    ↓
fixed-array sizing + uniform initialization
    ↓
compile { } interpreter
    ↓
const substitution into runtime AST
    ↓
sema / CFG codegen
```

`compile {}` 复用普通 SCMD AST，不执行文本宏或二次 parse。当前 interpreter 有 1,000,000 statement-step 上限，用于阻止失控的编译期循环。

固定数组在 codegen 中保持类型化 `CGArray`；constant index 直接选 element。0.11.1 对 `dst = array[index]` 这种直接动态读取使用平衡 control-flow selection tree，只在选中的叶子复制元素，避免先构造完整的 8-bit combinational mux；数组读取出现在更复杂表达式中时仍使用 mux lowering。dynamic write 使用 index branch dispatch。0.11 动态 array write 目前只支持 `=`。

`volatile u8` 禁止 storage narrowing，保留完整 8-bit storage identity。`@noopt function` 让其 owner alias 被 optimizer 标记为 preserve/opaque，从而跳过 forwarding / dedup / fusion / internal rename。

compile phase 不提供 shell/subprocess/host file-write escape；它的目标是替代项目 source generator，而不是嵌入一门新的宿主脚本系统。

## 0.10.x：u8 快路径 lowering

CS2 CFG 后端现在会在语义安全时优先生成更短的原地位操作，而不是统一先把结果复制到 8-bit 临时寄存器再回写。当前快路径包括：

- `u8` 直接复制与自复制消除；
- `x +/- 常量` 的原地 ripple carry / borrow；
- 常量左移、右移的方向感知原地回写；
- `* 0`、`* 1`、`* 2^n`；
- `/ 2^n` 与 `% 2^n`；
- lane-local `& | ^ ~` 和安全的浅层固定移位表达式。

复杂表达式仍保留原来的临时快照 fallback，因此这组优化不会改变 `u8` 的 8-bit wrap 语义。它特别适合 VM、解释器和 bitfield-heavy 程序：寄存器选择、opcode 拆位、固定移位不再因为一个简单赋值生成一整组临时 alias。

目前 **不会**把普通 SCMD 算术默认 lower 到 CS2 Sound Operator System。SOS 数值路径依赖 debug/cheat surface，而且 CFG alias 控制流还没有一个通用的 SOS-result branch bridge。`scmdsim` 已单独提供 SOS compatibility model，方便继续做实验后端而不污染默认的 portable CFG backend。

### CS2 单命令长度保护

CS2 Console 的单条命令不是无限长的。`cs2-2026` backend 现在把 **510 UTF-8 bytes** 作为可发出的最大命令长度：CFG optimizer 在做 alias-chain fusion 时会提前停止，最终输出阶段还会再次校验每条 `alias NAME "BODY"`。超过限制的源级单命令会在编译时直接报错，而不是生成一个 CS2 运行时才出现 `WARNING: Command too long... ignoring!` 的坏包。
