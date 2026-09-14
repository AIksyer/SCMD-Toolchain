# 编译器、构建与诊断

## 编译器

`scmdc` 编译器主体使用 C17；0.7 起 Console 模拟器使用 C++20；0.9 的 SCB compiler / register VM 也使用 C++20。推荐 Clang + CMake + Ninja。

初始化编译器仓库：

```powershell
.\scripts\init.ps1
```

Debug 构建：

```powershell
.\scripts\build.ps1 -Config Debug
```

Release：

```powershell
.\scripts\build.ps1
```

清理：

```powershell
.\scripts\build.ps1 -Clean
```

## 单文件编译

```text
scmdc input.scmd -o output.cfg
```

常用参数：

```text
--console-mode sync|async
--console-settle-ms N
--tick-ms N
--page-bytes N
--page-commands N
--exec-prefix PATH
```

项目构建更推荐把这些目标参数写进 `.scmdproj`。

## 诊断

SCMD 诊断会输出源码位置、原行和 caret：

```text
error: expected '(' after if, got identifier
 --> src/main.scmd:4:8
 4 |     if x
   |        ^
```

目标是保持 Kinal / Clang 风格的可读诊断，而不是只打印 `syntax error`。

0.9 尚未冻结稳定错误码体系；后续会加入 `SCMD-...` 诊断编号。

## 格式

编译器不会因为 `{` 是否换行而改变语义。

以下都能编译：

```scmd
if(x){
}
```

以及：

```scmd
if(x)
{
}
```

项目推荐后一种 Allman 风格。未来 `scmdfmt` 会负责格式统一。

## 编译器与模拟器分离

0.10.0 中 `scmdc` 负责语言编译、项目构建以及 CFG → SCB pack：

```text
scmdc build project.scmdproj
scmdc pack build -o project.scb
```

运行 lazy CFG source view 或最终 SCB package 使用独立工具：

```text
scmdsim build
# 或
scmdsim project.scb

> exec project
```

这样编译器与运行环境可以分别测试、发布和分析，同时 simulator core 只有一份实现。详见 [`scmdsim` Console 模拟器](simulator.md)。
