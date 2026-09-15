# SCMD

SCMD（Shortcut Command）是一门编译到 Source / Counter-Strike 2 Console CFG 的编程语言。

它最早是 2022 年写的一个 Shortcut Command 工具，后来逐渐变成了现在这套编译器和运行环境。

目前仓库里的主要工具：

* `scmdc`：编译器和项目构建工具
* `scmdsim`：CFG / SCB 模拟器
* `vcs16as` / `vcs16run` / `vcs16dump`：vCS-16/2 assembler、reference VM 与 dump 工具
* `vcs16scmd`：vCS-16/2 -> SCMD AOT backend


当前版本：`0.11.1`
SCB ABI：`1`

## 构建

需要：

* Clang / Clang++
* CMake 3.22+
* Ninja

Windows：

```powershell id="egwxtn"
.\scripts\init.ps1
.\scripts\build.ps1
```

构建后的程序在：

```text id="dhmxs3"
dist/release/scmdc.exe
dist/release/scmdsim.exe
dist/release/vcs16as.exe
dist/release/vcs16run.exe
dist/release/vcs16dump.exe
dist/release/vcs16scmd.exe
```

`out/` 只用于 CMake、Ninja 和测试产生的中间文件。

## 使用

创建一个项目：

```powershell id="xgdwtb"
scmdc init hello
cd hello
scmdc build hello.scmdproj
```

生成的 CFG 可以直接复制到 CS2 的 `cfg` 目录，然后正常执行：

```text id="rppd0x"
exec hello
```

也可以直接用 `scmdsim` 打开生成目录：

```powershell id="rt7yd9"
scmdsim build
```

```text id="o24lzm"
> exec hello
```

如果需要一个完整的二进制快照：

```powershell id="rr41z6"
scmdc pack build -o hello.scb
scmdsim hello.scb
```

## 实现

SCMD 最终不会生成 DLL，也没有需要注入游戏的运行时。

编译结果仍然是普通的 Source Console command：

```text id="4i3jfa"
alias
exec
echo
cvar
...
```

控制流、变量和算术最后都会被 lowering 成由 `alias` 组成的状态和跳转网络。

简单来说：

```text id="vrm08p"
SCMD
  ↓
parser / sema
  ↓
CFG backend
  ↓
Source Console / alias
  ↓
CS2
```

因此生成出来的程序本身就可以被真实 CS2 执行。

大型输出会被拆成多个 CFG page，避免一次向 Console command buffer 塞入过多命令。

CS2 后端现在**强制使用按需函数加载**：`main()`、全局状态、返回槽和函数入口 stub 在启动时加载；普通函数体放进 `lazy/fNNNN/` 模块，第一次调用时才 `exec`，随后入口 alias 会被真实实现覆盖。这个行为属于后端语义，不提供关闭选项；即使使用 `--no-opt`，按需加载仍然存在。

CS2 CFG 后端默认开启体积优化，会清理不可达节点、合并安全的连续跳转、去重静态 alias，并缩短编译器生成的内部符号；`wait`、`exec`、动态 alias 和分页安全仍由后端保留。需要调试或对比未优化输出时，可以使用 `scmdc ... --no-opt` 或 `scmdc build project.scmdproj --no-opt`。

## scmdsim

`scmdsim` 实现了一套和项目目标一致的 Console 环境，用于在游戏外运行和调试生成结果。

直接传 CFG root 时：

```powershell id="2kv8cb"
scmdsim build
```

不会在启动时把目录里的所有 CFG 全部编译一遍。

例如：

```text id="wabfy9"
> exec Scmd/Menu/Main
```

对应文件会在第一次执行时编译并缓存到当前进程。

修改文件后再次 `exec` 会重新读取；运行过程中新增的 CFG 也可以直接执行。

如果希望把缓存保留下来：

```powershell id="p2f917"
scmdsim build --cache
```

默认缓存目录：

```text id="f77pp7"
build/.scmdcache/
```

## SCB

SCB 是 `scmdsim` 使用的二进制格式。

目前的 SCB v1 不是另一套独立的 SCMD 后端。它从已经生成的 CFG 构建，保存 lowering 后的 Console 语义。

```text id="ksxij1"
SCMD
  ↓
CFG
  ↓
SCB
```

这样真实 CS2 和模拟器不会各自维护一套不同的执行逻辑。

SCB VM 目前使用 16 个寄存器。文件载入后会先经过 verifier，再转换到固定宽度的内部指令表示执行。

更详细的格式见：

[docs/zh-CN/bytecode.md](docs/zh-CN/bytecode.md)

## SCMD 0.11：编译期执行、固定数组与精确优化控制

0.11 的目标是让项目直接在 SCMD 源码里表达过去需要外部 source generator 才能完成的工作。编译期控制流继续使用普通 SCMD 语法，不引入第二套 `for`/range 语法。

```scmd
const N = 4;
u8 values[N] = 0;

compile
{
    for(var i = 0; i < N; i += 1)
    {
        values[i] = i + 1;
    }

    assert(values[3] == 4);
}
```

`compile {}` 在 `scmdc` 内执行，代码不会进入运行时 CFG。当前支持普通 SCMD 的 `if / while / for`、compile-local 变量、赋值、全局 fixed-array 读写和 `assert(expr)`。

固定数组当前为全局 `bool` / `u8`：

```scmd
bool used[16] = false;
u8 buffer[64] = 0;

function main()
{
    u8 i = 3;
    buffer[i] = 42;
}
```

需要保证真实 storage identity 的状态可以使用 `volatile`：

```scmd
volatile u8 host_state = 0;
```

它阻止 storage narrowing / 消除这类会让状态槽失真的优化，但仍允许不破坏可观察读写的安全优化。需要整个函数完全绕过 CFG optimizer 时使用：

```scmd
@noopt
function exact_host_bridge()
{
    host_state = 1;
    host_state = 2;
}
```

`@export` / `@resident` 也可作为函数属性；旧的 `export resident function` 语法继续兼容。

0.11 的 compile interpreter 不提供 subprocess / shell escape / 宿主文件写入；它用于确定性地构造当前程序的编译期状态，而不是把 `scmdc` 变成另一门脚本宿主。

## 示例

```scmd id="9gg3lg"
bool enabled = true;
u8 count = 10;

function main()
{
    console.clear();

    if(enabled && count >= 10)
    {
        console.print("hello from SCMD");
    }
}
```

SCMD 标识符使用 UTF-8：

```scmd id="mayyn0"
u8 数量 = 10;

function 输出()
{
    console.print("你好");
}
```

## 文档

文档入口：

[docs/zh-CN/index.md](docs/zh-CN/index.md)

实现细节、语言语法、项目格式和模拟器行为都放在 `docs/zh-CN/`。

## License

[MIT License](LICENSE)

### Demand-loaded static helper bundles

The CS2 backend's mandatory function demand loading co-loads statically called helpers when they
have no local storage. This prevents first-use `exec` diagnostics from being inserted midway
through console UI output while preserving lazy loading for independent/stateful functions.
The CS2 simulator models `[InputService] execing ...` for synchronous `exec`, so this behavior is
covered by regression tests.

### CS2 Console compatibility guardrails

`scmdsim` intentionally follows observed CS2 Console behavior rather than adding shell features:

- `|` is a literal argument, not a pipe operator.
- synchronous `exec` emits the modeled `[InputService] execing ...` diagnostic unless engine messages are explicitly disabled for a test.
- real CS2 builtins such as `help` and `kill` take precedence over same-name aliases in the simulator.
- `export function` rejects public names reserved by CS2/SCMD (`help`, `kill`, `clear`, `clearall`, `hideconsole`, `showconsole`, etc.).

These rules exist specifically to prevent a CFG from passing in `scmdsim` while behaving differently in the game.

### Resident functions and Console text semantics

Functions that must never demand-load after a timing-sensitive boundary can be declared resident:

```scmd
resident function redraw_core()
{
    console.print("ready");
}

export resident function public_resident_entry()
{
    redraw_core();
}
```

`resident function` bodies are emitted into the eager core. `export resident function` additionally exposes the stable public Console alias.

The simulator also models the real-CS2 UI distinction observed in-game:

- `echo text` renders as `[Console] text`.
- `echoln text` renders raw text without the `[Console]` prefix.

`console.print(...)` lowers to `echoln` and is the preferred API for terminal-style output.
