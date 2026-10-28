# Vim 基础操作实现与验证

2026-10-28 增量：顶部模式标签改为 `[N]`、`[I]`、`[V]`，四种 VC6 产品均重新构建成功，
六个 PE32/XP 元数据检查通过。本次未重复窗口功能回归；下方 152 项窗口检查属于 2026-10-27。
本轮 [清单](verification/statusbar-20261028/manifest.json)，测试包为 `build/deliveries/mijoe-vim-winxp-x86-20261028.zip`。

日期：2026-10-27。基于当前 main 工作区，尚未提交或推送。

## 实现范围

- 默认 Normal；Insert、字符 Visual、整行 Visual 和独立的 `:` 命令输入。
- `h/j/k/l`、方向键、`w/b/e`、`0/^/$`、`gg/G`、数字行号、半页移动。
- `f/F/t/T`、`;`、`,`，支持中文目标、次数、操作符和 Visual；`%` 支持嵌套括号、
  C 注释和条件编译配对，数字百分比按行跳转，失败取消待执行操作。
- 次数与组合操作、`x/dd/dw/d$/cc/cw/yy/yw`、内部寄存器 `p/P`、`u`、`Ctrl+r`、`.`。
- 文件打开、新缓冲区、稳定编号、切换、删除、未保存内容保护。
- 保存、副本写出、另存为、显式覆盖及保存成功后退出；写盘失败保留修改状态。
- Unicode 文本/路径、换行格式沿用原内核；粘贴作为数据处理，命令行粘贴换行不会直接执行命令。
- `-novim` / `:set novim` 恢复 JOE；仍只保留 `joe` 一个前端。
- `:help` / `:h` 提供嵌入式中文支持列表，PageDown/PageUp 翻页、q/Esc 关闭；
  `Ctrl+K H` 恢复 JOE 原始帮助，`Ctrl+T` 恢复选项菜单，`Ctrl+K T` 沿用缩小窗口。
- `:colors` / `:colorscheme` 打开主题菜单，或带名称直接切换；原有 `Ctrl+T` → S 路径共用主题应用函数。
- `Ctrl+V` 矩形选择及 `I/A` 列插入，Esc 后应用到所选各行；支持短行、Tab、中文宽字符、
  `$A`、整批撤销/重做和单行粘贴。矩形删除/修改/复制粘贴尚未开放。

公共模式与编辑逻辑位于 `joe/vim.c`，平台输入适配在各自的 `tty.c`。
Windows Visual Studio 与 UNIX Automake 均引用同一个 Vim 源文件。
修复了 UNIX 构建中缺失目录引用、过时的本地 Autoconf 宏及 macOS util.h 依赖。

## 已完成验证

| 项目 | 结果 | 证据 |
| --- | --- | --- |
| macOS ARM64 原生编译及安装 | 成功 | 本轮实际 configure/make/install；产物为 Mach-O arm64 |
| 原生真实终端回归与定位验证 | 48 项包回归、78 组定位对照及 6 项保护、14 项 UI、37 组列插入对照及 5 项保护，共 188 项通过 | [macOS 记录](../../macos/verification.md) |
| 真正 VC6 Release 独立版 | 构建及 38 项窗口测试通过 | [构建](verification/vim/vc6-release-package.log)、[窗口测试](verification/vim/ui-release-package.log) |
| 真正 VC6 Debug 独立版 | 构建及 38 项窗口测试通过 | [构建](verification/vim/vc6-debug-package.log)、[窗口测试](verification/vim/ui-debug-package.log) |
| 真正 VC6 Release DLL 版 | 构建及 38 项窗口测试通过 | [构建](verification/vim/vc6-release-dll.log)、[窗口测试](verification/vim/ui-release-dll.log) |
| 真正 VC6 Debug DLL 版 | 构建及 38 项窗口测试通过 | [构建](verification/vim/vc6-debug-dll.log)、[窗口测试](verification/vim/ui-debug-dll.log) |
| 二进制元数据 | 6 个 EXE/DLL 均为 PE32/i386，子系统 5.1；未发现检查清单中的新系统硬导入或外部 VC CRT 导入 | [pe-xp.json](verification/vim/pe-xp.json) |
| 新模块旧 C 语法兼容检查 | GNU89 下禁用语句后声明，检查通过；最终 VC6 日志没有 Vim 新增编译警告 | 实际 cc 检查和上述构建日志 |
| 发布 ZIP | 解压校验通过；逐文件与 Release 产品比较一致；只包含程序、主题、帮助和 README | 见下方发布包 |

Windows 窗口测试通过 Wine 启动本轮构建出的真正 Windows 程序，按进程 PID 找到自身子窗口后投递按键，
验证退出码和实际保存内容。每配置覆盖编辑、删除/撤销/重做/重复、两种 Visual、复制粘贴、保存失败、
多缓冲区、另存为、Windows 反斜杠路径及命令行粘贴文件名；另覆盖 f/F/t/T、重复查找、
次数、查找失败、括号/字符常量/注释/条件编译/数字百分比操作；本轮增加帮助翻页、帮助内撤销/粘贴保护、
原始帮助与窗口快捷键、原始主题入口、主题菜单、菜单返回 Insert；另增加列插入、短行、对齐追加、
行尾追加、Tab 拆分、撤销/重做、粘贴和取消 8 项。四种产品全部 152 项通过。
[windows-ui.json](verification/vim/windows-ui.json) 保存按键、预期字节和实际字节。

初始构建及后续增量均使用真实 VC6/NMAKE；归档的是最终增量构建日志。
原工程既有警告不在本轮全部消除，不能将构建成功理解为全工程零警告。
自动化测试没有改写用户 HOME 或保存用户的 joe_state。

## 复现

UNIX：按 [操作和构建说明](../../docs/vim.md) 构建后运行：

```sh
python3 tests/vim_pty_test.py /absolute/path/to/joe
python3 tests/vim_motion_test.py /absolute/path/to/joe
python3 tests/vim_ui_test.py /absolute/path/to/joe
python3 tests/vim_block_test.py /absolute/path/to/joe
python3 tools/generate_vim_help.py --check
```

Windows：`windows/vc6/build.bat Release package`；也可用
`tools/build_vc6_wine.py --help` 配置真实 VC6 与便携 Wine，`--smoke` 执行窗口测试。
`tools/verify_pe_xp.py` 检查产品架构与导入表。

生成器 `python3 windows/vc6/generate.py` 同步 VC6 工程、资源和离线 Vim 帮助。
已添加 Ubuntu/macOS CI 配置，尚未推送或运行远程 CI。

## Windows XP 验收

1. 将发布 ZIP 整体解压至 XP，启动 `joe.exe`，应显示 `[N]`。
2. `i` 输入中英文，`Esc` 返回；测试移动、`v/V`、复制删除、`u`、`Ctrl+r`、`.`。
   验证 `fx/Fx/tx/Tx`、`;`、`,`、`%`、`50%`、`dfx/dFx/d%` 及查找失败保护。
3. 测试 `:saveas` 中文/含空格路径、`:w`、`:wq`，重新打开核对文件内容。
4. 打开多个文件，用 `:ls`、`:bn/:bp`、`:b 编号` 切换；确认未保存修改仍在。
5. 测试中文输入法候选期间的 Esc、Shift+Insert 粘贴，以及命令行中的路径粘贴。
6. `joe.exe -novim` 应可使用传统 JOE 键位。
7. `:help` 应显示中文命令及简介，PageDown/PageUp 翻页、q/Esc 返回；Ctrl+K H 打开原始帮助。
8. Ctrl+T、S，输入 gruvbox 后回车；或用 `:colors` 菜单选择，观察实际界面颜色变化。
9. 行首 Ctrl+V、2j、I，输入前缀后 Esc；验证三行均插入，u / Ctrl+r 整批撤销、重做。

## 验证边界

Windows XP 实机或虚拟机尚未运行本轮产品，中文输入法实际交互仍需 XP 验收。
Wine 的 Unicode 文件名测试不能替代中文输入法测试。
Linux/BSD/Solaris 和现代 Visual Studio 构建尚未在本轮执行；Linux CI 配置未运行。
PE 元数据与导入检查不能替代目标 OS 的实际加载和操作验证。

矩形模式目前支持选择及 I/A 列插入，不包含矩形删除/修改/复制粘贴。
命名寄存器、文本对象、Vimscript、完整 Ex 命令或强制重新加载暂未实现。
搜索使用 JOE 标准正则引擎，不承诺完整 Vim 正则语法。次数主要面向移动和操作符。

## 产品

[源文件与产品哈希](verification/vim/manifest.json) 记录本轮源码、6 个产品及基线提交。
首选 XP 测试包：`build/deliveries/mijoe-vim-winxp-x86-20261027.zip`。
对应独立程序：`windows/bin/vc6/Release/joe.exe`。
离线操作说明：包内 `docs/vim.html`。

发布 ZIP SHA-256：`97395aa161dd58de0fd0830f5e8439e62c10ddb6f63c93f5083b9e7902641d3d`。
