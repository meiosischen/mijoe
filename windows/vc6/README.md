# JOE for Windows 4.6：VC6 编译说明

默认启用 Vim 基础操作；`joe.exe -novim` 使用传统 JOE 键位。
操作见 [Vim 使用说明](../../docs/vim.md)，最新构建和测试见
[Vim 验证记录](vim-verification.md)。`package` 中的 `docs/vim.html` 可离线查看。

这是 Win32/x86、面向 Windows XP 的 VC6 移植版。已使用真正的 Visual C++ 6.0
编译器、资源编译器、链接器及 Developer Studio，在 Wine 中完成构建和运行测试。
Windows XP 实机或 UTM 虚拟机运行尚未验证，详细证据见 [verification.md](verification.md)。

## 打开工程

1. 将整个 `joewin-4.6` 源码目录复制到 Windows，保留目录结构。
2. 用 VC6 打开本目录的 `joewin.dsw`。
3. 在 Build → Set Active Configuration 中选择 `joewin - Win32 Release` 或 Debug。
4. 按 F7 编译；需要完整重建时使用 Build → Rebuild All。

这是 Developer Studio 6.0 的外部 NMAKE 工程。工程名和配置名均为 `joewin`，
工作区保留了 VC6 要求的格式与 CRLF 换行。IDE 编译同时复制主题和离线帮助。

## 命令行构建

在已执行 VC6 `VCVARS32.BAT` 的命令提示符中运行：

```bat
cd windows\vc6
build.bat Release package
build.bat Debug package
```

也可以指定 VC6 安装目录；`VC6_ROOT` 支持 Visual Studio 根目录或其 `VC98` 子目录：

```bat
set "VC6_ROOT=C:\Program Files\Microsoft Visual Studio"
build.bat Release package
```

`build.bat Release` 只编译独立 EXE；`package` 另外复制 `colors` 和 `docs`。
可选的共享 DLL 版本用以下命令构建：

```bat
build.bat Release dll
build.bat Debug dll
```

`build.bat Release clean` 只清理该配置的 `windows\bin\vc6\Release` 构建输出。
使用 VC6 自带头文件与库即可；应用程序静态链接 VC6 CRT，无需安装 MFC 或额外的 VC 运行库 DLL。

Git 只保留可运行程序、主题、帮助和验证证据；`obj` 目录及 `.lib/.exp/.pdb`
等编译中间文件已清理并加入忽略规则，构建时会自动重新生成。

## 输出与使用

- 独立版：`windows\bin\vc6\Release\` 或 `Debug\`。
- 共享版：对应目录下的 `dll\`，运行时将 `joewin.dll` 与 EXE 放在同一目录。
- 只构建默认 JOE 键位的 `joe.exe`；共享版另生成 `joewin.dll`。
- 启动配置统一为 `joerc`，不随程序文件名或界面语言切换；界面翻译资源仍保留。
- 普通使用选择 `joe.exe`；程序内置启动配置、81 个语法文件和 18 个主题。
- 点击窗口左上角图标，在系统菜单的 **Color Schemes** 中切换主题；自定义 `.jcf`
  可放在 EXE 旁的 `colors\` 或 `%LOCALAPPDATA%\JoeEditor\colors\`。
- 独立版执行 `package` 后附带离线帮助。共享 DLL 版需要帮助文件时，将同配置的
  `docs\`、`colors\` 复制到 `dll\` 下。
- `joe` 的 `Ctrl+K` 后按 `!` 可执行普通外部命令，输出回到编辑缓冲区。

## 此次调整

禁用了 Windows 弹出虚拟终端、`joewinpty.exe` 构建和分发、终端尺寸管道，以及
F1～F4 的按键处理。前端会同时消费按下和松开事件，防止残留转义序列写入文本。
普通命令执行管道仍保留。原 `winpty` 源码保存在目录中，但不参与此版本构建。

VC6 兼容层处理旧 SDK 缺少的类型/API、64 位整数类型和 CRT 格式符、
无符号 64 位整数到浮点数的转换、资源编译器大小写与图标格式等差异。

原现代 Visual Studio solution 和安装工程也已去掉弹出终端引用；本次没有构建 WiX 安装包。
修改和新增文件的完整清单见 [change-list.txt](change-list.txt)。

## 重新生成输入文件

本目录已包含 `.dsp/.dsw`、版本头文件、资源脚本、压缩资源、VC6 图标及离线帮助。
在 XP/VC6 上直接构建不需要 Python、MSBuild、.NET 或联网下载。

修改原始配置、主题、语法文件或版本后，维护者在有 Python 3 的机器上运行：

```text
python3 windows/vc6/generate.py
```

生成器从原 Visual Studio 工程读取 C 源文件与资源清单；未变化的生成文件保留原时间戳。
随后重建对应配置即可。
