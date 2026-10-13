# VC6 移植验证记录

日期：2026-10-11。对象：当时的 JOE for Windows 4.6，禁用弹出终端和 F1～F4。

本文件保留收敛键位前的历史验证结果。2026-10-13 起只保留默认 JOE，
当前验证见 [default-joe-verification.md](default-joe-verification.md)。

## 环境与结果

使用本机 VC6 工具目录中的真正 `CL.EXE` 12.00.8168（80x86）、`LINK.EXE`、
`LIB.EXE`、`RC.EXE`、`NMAKE.EXE` 与 `MSDEV.COM`，通过 Wine 执行。
应用程序使用 VC6 自带 SDK 头文件/导入库以及 `/MT`、`/MTd` 静态 CRT。
编译器版本输出保存在 [compiler-version.log](verification/compiler-version.log)。

| 验证项目 | 实测结果 | 证据 |
| --- | --- | --- |
| VC6 IDE 加载 `.dsw` 并全量重建 Release | 6 个独立 EXE，0 error、35 warning | [ide-release.log](verification/ide-release.log) |
| VC6 IDE 加载 `.dsw` 并全量重建 Debug | 6 个独立 EXE，0 error、35 warning | [ide-debug.log](verification/ide-debug.log) |
| NMAKE Release/Debug 共享版 | 每配置生成 `joewin.dll` 和 6 个前端 EXE，成功 | [release-dll.log](verification/release-dll.log)、[debug-dll.log](verification/debug-dll.log) |
| 全部二进制的架构与导入表 | 26 个文件均为 PE32/i386；无 `CancelIoEx`、`GetConsoleWindow`、`joewinpty` 或外部 VC CRT 导入 | [pe-check.log](verification/pe-check.log) |
| 四种 `joe` 构建运行 | Release/Debug × 独立/DLL，均启动、编辑、保存、退出成功 | `verification/full-*.log` |
| 其余五种配置启动 | 20 个组合全部打开编辑窗口 | `verification/profiles-*.log` |
| 中文文件名 | 经 Win32 Unicode 命令行打开中文文件名，保存后逐字节比较内容通过 | `verification/full-*.log` |
| 主题 | 系统菜单出现 18 个主题；实际选择 `darkblue2`、`default`，收到 JOE 处理后的菜单选中确认 | `verification/full-*.log` |
| F1～F4 | 发送四个键的按下/松开事件，无子进程；保存内容完全一致，无残留 `~` | `verification/full-*.log` |
| 普通外部命令 | `Ctrl+K !` 执行 `echo VC6-cmd-output`，保存内容含命令输出 | [command-run.log](verification/command-run.log) |
| 内置资源解压 | 实际 VC6 编译的 JOE/miniz 加载器解出 104 个资源，全部与原文件逐字节一致 | [resources-byte-check.log](verification/resources-byte-check.log) |
| 64 位计算与格式化 | 无符号 64 位边界、二进制/八进制解析、CRT `%I64` 格式化通过 | [numbers.log](verification/numbers.log) |
| 生成器重复执行 | 文件内容与时间戳保持一致 | [regeneration.log](verification/regeneration.log) |
| 原目录完整性 | 4.6 原始备份的 486 个文件哈希一致；3.8 的 814 个文件及哈希未改变 | [integrity.log](verification/integrity.log) |

二进制哈希清单保存在 [binary-sha256.txt](verification/binary-sha256.txt)。
验证辅助程序的 C 源码及本次调用脚本保存在 `verification/test-sources/`。
这些脚本记录了本机路径与 Wine 映射，换环境重跑时须调整工具和临时目录路径。

主题验证辅助 DLL 仅加载到测试程序自己启动的编辑器进程，检查本进程菜单与命令响应，
避免 Wine 的跨进程菜单读取限制；它不属于产品，未放入便携程序包。
每次验证前删除同 PID 的旧日志，再等待此次主题切换结果。

## 实测中修复的问题

- 原 `.dsw/.dsp` 生成方案沿用已经验证的 Developer Studio 6.0 格式；本次实际通过
  `MSDEV.COM /MAKE ... /REBUILD` 检查加载及重建。
- VC6 对无符号 `__int64` 直接转浮点数会在代码生成阶段报错，已按高/低 32 位转换，
  并用原计算器入口验证边界值。
- VC6 RC 会将资源名转为大写；资源查询已适配，压缩内容用实际加载器解压校验。
- 只删除快捷键绑定会留下转义序列的 `~`，已在 Windows 前端拦截 F1～F4。
- `jicerc` 的三处旧字节范围不符合 4.6 的 UTF-8 配置解析规则，已换成 Unicode 范围；
  四种 `jice` 构建均复测启动成功。

## 验证边界

Windows XP 实机/UTM 中的加载、中文输入法、打印与所有编辑功能没有逐项验证。
当前结果证明真正 VC6 的编译、资源处理、链接、IDE 工作区加载，以及上述 Wine 运行路径。
中文文件名测试中的编辑内容为 ASCII，不代表已验证中文输入法。

全量构建仍有 35 个原代码警告，主要是未使用变量和转换；其中两处 PuTTY 的
`char *`/`wchar_t *` 类型不匹配警告仍保留。没有将“0 error”表述为“0 warning”。
现代 Visual Studio 工程、WiX 安装包及 UNIX 构建未执行。
