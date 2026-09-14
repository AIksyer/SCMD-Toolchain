# SCMD 中文文档

SCMD（Shortcut Command）是一门以 Source / CS2 Console CFG 为主要目标的编译语言项目。

当前文档对应 **SCMD Toolchain 0.10.0**（`scmdc` + `scmdsim`），SCB 字节码 ABI 为 **1**。

## 阅读顺序

1. [语言基础](language.md)
2. [表达式与类型](expressions.md)
3. [Block](block.md)
4. [项目系统](project.md)
5. [CS2 后端](backend-cs2.md)
6. [SCB 字节码与寄存器 VM](bytecode.md)
7. [`scmdsim` 模拟器](simulator.md)
8. [编译器与诊断](compiler.md)
9. [当前状态与限制](status.md)
10. [测试](testing.md)

## 设计原则

- 源码表达意图，Valve 的兼容性问题留给 backend。
- `if` / `while` / `for` 条件必须有括号。
- 推荐 Allman 大括号，但排版不是语义；`if(x){}` 合法。
- 标识符支持 UTF-8 / 中文。
- 关键字当前统一小写英语；Lexer / Token 为未来多语言关键词保留入口。
- `get`、`var`、Block 思想继承 Kinal，但 SCMD 拥有独立语义和目标后端。
- simulator 必须验证**与 CFG/CS2 相同的 lowered semantics**，不能为了速度偷偷把复杂 alias 网络替换成宿主高级运算。
