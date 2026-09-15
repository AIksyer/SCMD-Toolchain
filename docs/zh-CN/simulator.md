# `scmdsim`：SCB VM + CS2 Console / CFG 模拟器

`scmdsim` 是独立公开工具：

```text
scmdc      compile / build / pack
scmdsim    lazy CFG compile / bytecode / run / interact
```

执行核心仍是 **SCB v1 16-register VM**。0.11.1 的变化主要在开发期加载策略：CFG root 从“启动时全量 AOT”改成“`exec` 时按 module 编译”。

## 启动方式

### CFG root：推荐开发模式

```powershell
scmdsim build
```

会立即进入交互 Console。启动阶段只建立最小 runtime，不编译所有 CFG。

```text
> exec Scmd/Menu/Main
```

第一次执行某个 module 时：

```text
resolve path -> read one .cfg -> compile SCB block -> cache in process -> execute
```

再次执行未修改 module 直接复用。文件 size/mtime 改变后下一次 `exec` 自动重新编译。模拟器运行过程中新增 `.cfg` 也可以直接执行。

无位置参数时 root 为当前目录：

```powershell
scmdsim
```

### 持久 module cache

跨进程开发可选：

```powershell
scmdsim build --cache
```

默认缓存目录：

```text
build/.scmdcache/modules/
```

自定义：

```powershell
scmdsim build --cache-dir K:\cache\scmd
```

缓存 key 包含规范化 module 名和 CFG 内容，内容变化会 miss 并重新编译。默认**不开缓存**：对一次性/小 module，读写 SCB cache 可能比直接编译更贵。

查看：

```text
:cache
:stats
```

### 显式全量预编译

```powershell
scmdsim build --precompile
```

交互中：

```text
:precompile
```

生成不可变 SCB：

```powershell
scmdc pack build -o demo.scb
# 或
scmdsim build --save-scb demo.scb
```

### 直接运行 SCB

```powershell
scmdsim demo.scb
```

SCB 模式会验证 package 并使用其 module table，不再观察磁盘 CFG 变化。

## `exec` 与路径

支持：

- `exec`
- `execifexists`
- `exec_async`
- `/` 与 `\` 归一化
- `.cfg` 后缀可省略
- ASCII case-insensitive module name
- 拒绝绝对路径、drive/colon 和 `..` 越界

CFG-root 模式的直接 path hit 不需要扫描全目录；只有大小写回退或补全时才枚举文件名。

## Tab 补全

首 token 可补：builtin / runtime alias / 已知 cvar。

`exec*` 的参数做目录感知路径补全：

```text
> exec S<Tab>
> exec Scmd/
> exec Scmd/M<Tab>
```

CFG-root 模式每次补全动态扫描**文件名**，因此新放入的 CFG 会立即出现；不会为了补全编译内容。SCB 模式使用 package module table。

测试入口：

```text
:complete exec Scmd/
:modules Scmd/
```

## 退出 / line editor

```text
quit
exit
:quit
:q
:exit
```

都能退出。

- 空输入行 `Ctrl+C`：退出 simulator。
- 当前行已有文本 `Ctrl+C`：取消当前行。
- Tab：补全。
- Up / Down：历史。
- Backspace：按 UTF-8 codepoint 删除。
- Windows：`_getwch` + UTF-8 输出。

## 模拟器 meta commands

```text
:stats
:time
:aliases [prefix]
:cvars
:modules [prefix]
:complete <line>
:precompile
:cache
:help
:quit
```

## Compatibility profile

当前正式 profile：

```text
cs2-2026
```

普通输入按 CS2 Console compatibility subset 处理，而不是 VM assembly shell。

## 当前 Console 子集

- `alias`
- `exec` / `execifexists` / `exec_async`
- `sleep`
- `clear`
- `echo` / `echoln`
- `incrementvar` / `multvar` / `toggle`
- `setinfo`
- `say` / `say_team`
- 少量 dynamic cvar

未知原生 CS2 命令会报告 Unknown；这只表示 simulator 没建模，不代表真实游戏不存在。

## 已固化的 CS2 quirks

- alias 尾随 argv 不进入 alias body。
- 当前 `|` 按普通文本，不模拟已不存在的 pipe；回归测试同时验证 `echo x | setinfo y` 只会原样 echo，右侧 `setinfo` **不得执行**。
- `exec_async + sleep` 使用 deterministic virtual scheduler，不真实等待墙钟。

## SCB

SCB v1 仍是 canonical binary execution format：magic / ABI / profile / string pool / blocks / module table / fixed predecoded instructions / checksum。CFG-root lazy module 最终也是编译到相同 VM representation 后执行。

## SOS compatibility model（实验）

`cs2-2026` profile 现在包含一套 **Source 2 Sound Operator System (SOS) compatibility model**。它不是音频渲染器，也不是对 Source 2 内部实现的完整复刻；目标是把 2026 CS2 实机实验中已经验证的数值/数据流行为固化进 `scmdsim`，这样相关 CFG/Console 实验不必每次都进游戏验证。

当前建模范围包括：

- `snd_sos_print_operators` / `snd_sos_print_operator_stacks` / stack 与 operator dump；
- `snd_sos_set_operator_field`（float/bool/enum；string 仍按实机表现拒绝）；
- `snd_sos_resolve_execute_operator`；
- `math_float`、`math_filter_float`、`math_remap_float`、`math_string`、float switch；
- `opvar_get_float` / `opvar_set_float` / `opvar_increment_float`；
- local opvar 普通字段读写；
- `diagnostic_globals/test_opvars` 的标量与数组 fixture、indexed load；
- `convar_get` / `convar_set` 数值桥；
- `snd_opvar_set` 的 `SetOnSpawn` 延迟字段 patch（包括非空 string）；
- 持久 `snd_opvar_set` entity 的 `SetStackName/SetOperatorName/SetOpvarName/SetOpvarIndex/ChangeOpvarValueAndSet/SetOpvar` 输入，可用于复现 deferred indexed STORE；
- 已观察到的 write-context 差异：`update_test_opvar -> diagnostic_globals` 可写，而 soundscape stack 对同一 global target 只读；
- SOS 数值字段按 float32 精度存储，保留约 `2^24` 的整数精确边界；
- `sos_import_stack` 的已解析 graph 采用静态展开模型：运行时修改 `import_stack` 字符串不会重建 flattened child graph；
- `cl_sos_test_set_opvar/get_opvar` 保留实机的 `!!!FIXME: SOSSetOpvarFloat API not ported` 行为。

`ent_create snd_opvar_set ... setOnSpawn 1` 的 patch 在一条提交的 Console 输入完全 drain 后再生效，因此同一行后续 dump 仍能看到旧值，下一行看到新值。这是为了复现我们实机看到的 deferred entity/SOS update 语义。

另外，`cs2-2026` CFG loader 也复刻 CS2 的单命令 tokenizer 上限：命令长度超过 **510 UTF-8 bytes** 时会输出 `WARNING: Command too long... ignoring!` 并忽略该命令。这样 optimizer 如果再次生成不合法的超长 alias，simulator 不会替真实 CS2“错误地执行成功”。

这套模型刻意只编码**已经验证过的 fixture 与 quirks**。未知 SOS stack/operator 仍可能与真实 CS2 不同；需要扩展时，应先用实机日志确定语义，再把测试和 fixture 一起加入 simulator。
