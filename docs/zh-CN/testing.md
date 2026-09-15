# 测试

项目使用 CTest。

```powershell
.\scripts\build.ps1 -Config Release
```

Sanitizer：

```powershell
.\scripts\build.ps1 -Config Sanitize
```

## 0.11 测试面

当前 Release CTest：**66 项**。

除原有语言、CFG optimizer、Console compatibility、SCB/VM、vCS-16/2、lazy loading 与 simulator 测试外，0.11 新增：

- `const + compile {}` 正常执行；
- ordinary SCMD `for(var i = ...; ...; ...)` 在 compile phase 中执行；
- compile-time fixed-array 初始化进入 runtime 初始状态；
- dynamic fixed-array read/write；
- `volatile u8` 保持完整 8-bit storage；
- `@noopt` function 的内部 alias 不被 optimizer fuse/dedup/rename；
- unsupported dynamic array compound assignment 明确拒绝。

Compile-time regression 必须输出：

```text
COMPTIME_ARRAY_PASS
VOLATILE_NOOPT_PASS
```

## Console compatibility

覆盖：

- `|` literal；
- Source builtin precedence；
- `echo` 显示 `[Console]`、`echoln` raw text；
- nested/lazy `exec` InputService 模型；
- alias rebinding / quoted semicolon；
- async/sleep virtual-time ordering；
- command length / budget / path traversal / case-insensitivity。

## 发布门槛

0.11 release 至少要求：

```text
Release   66 / 66 PASS
Sanitize  66 / 66 PASS
```

并保持 warnings-as-errors。Sanitize 使用 ASan + UBSan；支持的平台同时启用 LeakSanitizer。

## Real CS2

`scmdsim` regression 不能替代真实游戏验证。任何新增 Valve/Source Console 行为依赖仍需要 real-CS2 probe。
