# 测试

项目使用 CTest。

```powershell
.\scripts\build.ps1
```

Sanitizer：

```powershell
.\scripts\build.ps1 -Config Sanitize
```

## 0.10 测试面

当前 CTest **44 项**，覆盖：

### 语言 / backend

- 核心语法
- `u8` 算术/比较/位运算
- runtime-safe ripple add/sub
- dynamic `* / %` bounded lowering
- `while` / `for`
- Block run/jump
- 同行 `{`
- `if` 缺括号拒绝
- multifile `get`
- Unicode identifier
- recursion rejection
- constant edge semantics
- deterministic arithmetic matrix

### Console compatibility

- alias trailing argv ignored
- `|` literal
- alias self-rebinding
- quoted semicolon
- nested exec
- `exec_async + sleep`
- virtual-time ordering
- `incrementvar` wrap
- command budget
- traversal rejection
- case-insensitive command/alias
- missing `execifexists` nonfatal
- script `:quit`
- profile accept/reject

### SCB / VM

- `scmdc pack` CFG -> SCB
- CFG / SCB output equivalence
- malformed SCB rejection
- `exec` path completion
- no-argument simulator defaults to cwd
- vCS-16 A1 lazy-CFG bring-up
- vCS-16 A1 precompiled-SCB bring-up
- plain `quit` / `exit`
- lazy CFG startup
- persistent cache hit / invalidation / new-module discovery

vCS 两条路径都必须输出：

```text
vCS-16 A1 CORE: PASS
```

## Arithmetic matrix

动态 operand 防止 constant folding 偷掉 runtime lowering；覆盖 80 组 operand pair：

```text
+ - * / % & | ^
```

以及动态 shift `0..10` / `255`、除零、carry/borrow/wrap。

必须输出：

```text
ARITH_MATRIX_PASS
```

## 发布门槛

0.9 release 必须至少完成：

```text
Debug      44 / 44 PASS
Release    44 / 44 PASS
Sanitize   44 / 44 PASS
```

并启用 warnings-as-errors。

Sanitize 使用 ASan + UBSan；Linux/Clang 环境启用 LeakSanitizer：

```text
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
UBSAN_OPTIONS=halt_on_error=1
```

## Real CS2

`scmdsim` regression 不能替代真实游戏验证。新增/依赖 Valve 行为时仍需 real-CS2 probe。
