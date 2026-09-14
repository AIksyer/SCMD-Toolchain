# SCMD runtime notes (0.6)

SCMD 的运行时不是独立二进制，而是一组由 backend 生成的 CFG / alias / async continuation 文件。

0.6 不再有 `render {}` / `screen {}` 语言结构。

Console 重绘兼容由 backend 自动处理：

```text
console.clear()
  -> async worker
  -> clear
  -> sleep <settle>
  -> continuation
```

`wait` 使用同一 continuation worker 机制。

普通 bool/u8 运算、分支、循环、函数和 Block 控制流仍然是 alias 逻辑，不主动 sleep。

详见 `docs/zh-CN/backend-cs2.md`。
