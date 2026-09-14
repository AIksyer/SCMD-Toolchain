# SCMD

SCMD（Shortcut Command）是一门编译到 Source / Counter-Strike 2 Console CFG 的编程语言。

它最早是 2022 年写的一个 Shortcut Command 工具，后来逐渐变成了现在这套编译器和运行环境。

目前仓库里主要有两个程序：

* `scmdc`：编译器和项目构建工具
* `scmdsim`：CFG / SCB 模拟器

当前版本：`0.10.0`
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
