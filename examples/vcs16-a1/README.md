# vCS-16 A1

> **Legacy/reference implementation.** A1 intentionally models a conventional CPU (PC/fetch/decode/FLAGS). It remains in the tree as a compatibility and regression experiment. New development uses the library-based, CFG-oriented [vCS-16/2](../../docs/zh-CN/vcs16-v2.md) architecture instead.

vCS-16 A1 是运行在 SCMD/CS2 CFG alias runtime 上的 16-bit 虚拟 CPU。16-bit word 使用两个 `u8` 表示，包含 8 个 GPR、16-bit PC、Z/N/C/V flags、ALU、branch、LDI/MOV 和 HALT。

## 当前优化

A1 保持 ISA 与测试程序不变，但对最昂贵的 decode 路径做了 CFG 友好的结构优化：

- register file 的 `r0..r7` 选择从 8 个完整 `u8 == constant` 比较改为 3-bit decision tree；
- ROM fetch 从线性 `pc == address` 链改为按地址位展开的 balanced tree；
- 编译器本身会对固定移位、bitwise、直接 copy、常量 add/sub 和 power-of-two 算术走原地快路径。

在当前仓库的 A1 self-test 上，这些改动把生成 CFG 从原始约 **400,775 bytes / 7,410 aliases / 280 CFG files** 降到**270,065 bytes / 4,632 aliases / 178 CFG files**，同时保持。这里的结果已经包含 CS2 **510-byte 单命令上限**保护；优化器不会再为了减少 alias 数而生成会被游戏丢弃的超长 `alias` 定义。

同时保持：

```text
CPU HALT: PASS
R0 = 0x0037 (55): PASS
R1 = 0x000B (11): PASS
PC = 0x000E after HALT: PASS
vCS-16 A1 CORE: PASS
```

这里没有把 SOS 当成 A1 的默认 ALU。SOS 目前更适合作为实验协处理器/未来 backend：它已经能提供 float32 数值运算、opvar 状态、indexed load/store 机制和 ConVar bridge，但仍受 debug/cheat surface 与跨-stack write context 限制。
