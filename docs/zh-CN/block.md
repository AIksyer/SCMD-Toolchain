# Block

Block 是 SCMD 从 Kinal 继承并针对 Source `alias` 目标重新落地的一等控制流概念。

它不是普通 `{}` 作用域，因此保留独立视觉边界：

```scmd
block flow
</
    record start;
    console.print("start");

    record done;
    console.print("done");
/>
```

## Record

`record` 声明 Block 中可寻址的位置：

```scmd
record menu;
record settings;
record about;
```

0.6 要求 `record` 只出现在 Block 顶层，不能嵌套在 `if` / `while` / `for` 中。

## `run()`

从 Block 开头执行到末尾，然后返回调用点：

```scmd
flow.run();
```

## `jump()`

从某个 Record 开始执行：

```scmd
flow.jump(flow.done);
```

底层可以直接降为 alias / continuation 入口，因此这项能力与 SCMD 的目标机器非常契合。

## Block 内 `jump`

```scmd
block flow
</
    record begin;
    console.print("A");
    jump end;
    console.print("不会执行");

    record end;
    console.print("B");
/>
```

内部 `jump` 是无条件跳转。

## 当前预留但未实现

Parser 已认识这些 API 名，但 0.6 CS2 backend 会明确拒绝：

```scmd
flow.runUntil(flow.done);
flow.runRange(flow.begin, flow.done);
```

未来计划语义：

- `runUntil(end)`：`[block-start, end)`
- `runRange(start, end)`：`[start, end)`

## 捕获与生命周期

0.6 的 named Block 只能定义在顶层，因此它可以按引用访问全局变量。

函数局部 Block、Block 变量、Block 数组、可重入 Block 尚未实现。
