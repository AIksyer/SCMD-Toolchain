# CS2 后端

## alias 是目标机器

SCMD 将高层变量、布尔逻辑、整数电路、函数 continuation、Block Record 等能力降到 Source Console 的 `alias` 模型。

用户代码不应该依赖生成 alias 的具体名字。

## Console clear 的 CS2 问题

实机测试发现，快速重复执行 `clear` + 大量 Console 输出时，CS2 Console 可能出现空白、叠字或重绘问题。

因此 SCMD 不再要求用户写 `render` / `screen` 之类的 workaround 语法。

源码只写：

```scmd
console.clear();
console.print("hello");
```

`async` backend 会自动生成类似：

```text
exec_async worker.cfg

worker.cfg:
    clear
    sleep 16
    <continuation>
```

也就是说，Valve 的 bug 属于 backend implementation detail。

## `wait`

```scmd
wait 100ms;
```

同样会切断当前 continuation，并通过 async worker 在延时后继续。

## sv_cheats

当前实测 `exec_async` 属于受限制能力，因此：

- `console.mode = async` 的 `console.clear()` workaround 依赖本地/允许的环境。
- `wait` 依赖 async worker。
- 普通纯同步逻辑、`console.print()`、bool/u8 电路本身不需要 async。

可以将项目设置为：

```scmd
console
{
    mode = sync;
}
```

但这样会放弃 clear settle workaround。

## Command buffer 分页

大型生成 CFG 直接 `exec` 时，实机出现过：

```text
WARNING: Command buffer full... ignoring!
```

SCMD 因此默认把 alias 定义拆成多个 page，并链式 `exec`：

```text
entry.cfg
  -> pages/000.cfg
  -> pages/001.cfg
  -> ...
  -> main entry alias
```

这也是 `.scmdproj` 中 `paging` 配置存在的原因。


## 0.7.0：运行时安全的加减法 lowering

早期 0.6 的 `u8 + u8` / `u8 - u8` 会把整条 carry chain 表示成一个深层布尔表达式树。离线语义完全正确，但真实 CS2 在某些输入路径上会漏掉深层 alias dispatch。

0.7.0 改为：

```text
snapshot A/B
  ↓
bit0: sum + carry1
  ↓
bit1: sum + carry2
  ↓
...
  ↓
bit7
  ↓
commit destination
```

这样每一级只依赖已经物化的 bit/carry alias，执行路径更浅、更接近真实 ripple-carry datapath。代码量增加，但对 Source Console backend 更稳。
