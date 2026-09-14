# 项目系统与 `.scmdproj`

SCMD 的项目文件使用自己的声明式语法，而不是 INI。

```scmd
project "demo"
{
    entry = "src/main.scmd";
    output = "build";
    package = "demo";
    bootstrap = true;

    target cs2
    {
        console
        {
            mode = async;
            settle = 16ms;
            tick = 16ms;
        }

        paging
        {
            max_bytes = 4096;
            max_commands = 40;
        }
    }
}
```

## 字段

### project

- `entry`：源码入口。
- `output`：构建输出目录。
- `package`：放进 `cfg/` 后的内部包目录名。
- `bootstrap`：是否在输出根目录生成一个引导 CFG。

### target cs2 / console

- `mode = async|sync`：Console clear 的调度策略。
- `settle = 16ms`：`clear` 后继续执行前的稳定时间。
- `tick = 16ms`：`wait Nticks` 的 tick 量化值。

### paging

- `max_bytes`：单个 CFG page 的目标字节上限。
- `max_commands`：单个 CFG page 的顶层命令上限。

这两个值用于规避 CS2 的 `Command buffer full`。

## 构建

```text
scmdc build demo.scmdproj
```

典型输出：

```text
build/
├─ demo.cfg
└─ demo/
   ├─ entry.cfg
   ├─ manifest.json
   ├─ pages/
   │  ├─ 000.cfg
   │  ├─ 001.cfg
   │  └─ ...
   └─ async/
      ├─ 000.cfg
      └─ ...
```

`demo.cfg` 只负责：

```cfg
exec demo/entry
```

如果 `bootstrap = false`，根目录不会生成这个引导文件，启动命令改为：

```text
exec demo/entry
```

## 源码依赖不写进项目文件

`.scmdproj` 只指定入口：

```scmd
entry = "src/main.scmd";
```

源码依赖由：

```scmd
get "logic/math.scmd";
```

维护。这样不会同时维护一份 project source list 和一份源码 import graph。

## 初始化项目

```text
scmdc init MyProject
```

自动创建：

```text
MyProject/
├─ MyProject.scmdproj
├─ build.ps1
├─ build.sh
├─ .gitignore
└─ src/
   ├─ main.scmd
   └─ hello.scmd
```
