# 0.10 当前实现状态

## 语言已实现

- `get` 多文件加载 / flat namespace
- `bool` / `u8` / `var`
- UTF-8 / 中文标识符
- `function name()`
- `if(...) / else`
- `while(...)`
- `for(init; cond; step)`
- `return;`
- `+ - * / %`
- `== != < <= > >=`
- `! && || ^(bool)`
- `~ & | ^ << >>`
- 复合赋值
- `block </ />`
- `record`
- Block 内 `jump`
- `Block.run()`
- `Block.jump(Block.Record)`
- `console.print()` / `console.clear()`
- `chat.send()` / `teamchat.send()`
- `command.exec()`
- `wait Nms/Ns/Nticks`
- `.scmdproj`
- command-buffer-safe page loader

## 工具链已实现

- `scmdc` C17 frontend/backend
- `scmdsim` C++20 独立模拟器
- SCB ABI 1
- `scmdc pack CFGROOT -o file.scb`
- CFG root -> lazy per-module SCB compile + in-process cache
- precompiled `.scb` load / verify
- 16-register VM
- fused Console bytecode ops
- deterministic async virtual scheduler
- `exec` lazy module compile / hot reload；SCB 模式 module table dispatch
- Tab command / alias / cvar completion
- `exec` / `execifexists` / `exec_async` path completion
- command history
- SCB checksum / bounds verifier
- 运行中新增/修改 CFG 自动发现与下一次 `exec` 重编译
- 可选 `.scmdcache/modules/` 持久 module cache
- `--precompile` / `:precompile` / `--save-scb` 显式全量 AOT
- plain `quit` / `exit` 与 Ctrl+C line-editor 退出

## 暂未实现

语言：

- 函数参数
- 函数返回值表达式
- 递归调用栈
- `Block.runUntil()`
- `Block.runRange()`
- nested `record`
- Block 变量 / Block 数组 / 局部 Block
- `break` / `continue`
- module namespace / `get ... as ...`
- 多语言关键字包
- 一等 `string`
- formatter / LSP
- 稳定诊断错误码

字节码 / simulator：

- 从正式 lowered IR 直接双发 CFG/SCB（0.9 仍以 emitted CFG 为 canonical SCB 输入）
- SCB 压缩 / 变长磁盘编码
- bytecode decompiler / CFG -> SCMD decompiler
- 完整 Source 2 Console command set
- full cursor-editing readline UI

## CFG-root / SCB 注意事项

`scmdsim cfg-root` 在 0.10 中是开发期 **lazy source view**：启动不全量编译；`exec` 时读取目标 CFG，size/mtime 改变后自动重编译，新增文件也可直接执行 / Tab 补全。

预编译 `.scb` 则是不可变 package snapshot；如果需要固定发布物或 benchmark，使用 `scmdc pack` / `--save-scb`。
