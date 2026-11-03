# 顶部路径选择与复制验证

日期：2026-11-03。

## 本次行为

- 顶部 `%n` 显示绝对文件路径，不再缩写为 `~`。长路径截断显示时保留修改、只读等状态字段。
- 图形窗口和控制台可拖选路径，松开后复制；图形窗口双击自动复制完整路径，包括未显示部分。
- 路径选择独立于正文的光标、块标记及 Vim 模式；新的正文鼠标或 Vim 键盘操作取消路径选择。另存为后不复制旧路径。
- macOS 使用原生剪贴板；Windows 使用 `CF_UNICODETEXT`；控制台使用 OSC 52，需 `-mouse` 和终端支持。终端自身选择可使用 Shift 拖选。
- 修复 Vim 按键解析吞掉 SGR/X10 鼠标报告，以及 OSC 52 对 UTF-8 高位字节的编码错误。

## 构建与回归

| 项目 | 结果 | 证据 |
| --- | --- | --- |
| macOS ARM64 TUI / AppKit 构建 | 成功；构建清单与源码、TUI 二进制哈希匹配 | [build-info.json](../macos/verification/status-path-20261103/build-info.json) |
| 路径专用实际编辑器测试 | 11 项通过；双向拖选、N/I/V、双击长路径、中文宽字符、特殊文件名、另存为、SGR/X10、OSC 52 | [tests-path.log](../macos/verification/status-path-20261103/tests-path.log) |
| 原生 AppKit 离屏集成 | 121 项通过，含真实视图双击、系统剪贴板、路径选色和状态栏修改标记 | [native-view.json](../macos/verification/status-path-20261103/native-view.json)、[截图](../macos/verification/status-path-20261103/status-path.png) |
| Vim / GUI bridge / 帮助菜单 / grep / 退出回归 | 46 / 13 / 32 / 14 / 8 项通过 | [验证目录](../macos/verification/status-path-20261103/) |
| VC6 Release/Debug × 独立/DLL | 四种构建成功；每种均通过中文完整路径双击复制、Visual、正文复制粘贴、命令行粘贴，共 16 项 | [windows-final.log](../windows/vc6/verification/status-path-20261103/windows-final.log) |
| Windows 二进制静态检查 | 六个 PE32/i386 文件，子系统不高于 5.1，无检查名单内的 XP 之后 API 或外部 VC CRT 硬导入 | [pe-check.json](../windows/vc6/verification/status-path-20261103/pe-check.json) |
| `/usr/local/bin/mj` | 已验证软链指向最新构建的 `bin/joe` | [command-link.json](../macos/verification/status-path-20261103/command-link.json) |

macOS 测试通过 PTY、私有控制管道和离屏 AppKit 视图运行；未重启用户正在运行的已安装 App。
新窗口版位于 `build/macos/MiJoe.app`。Windows 动态测试在 Wine 中执行，未在真实 XP 上验收。

重跑路径测试：

```sh
python3 tests/status_path_test.py build/macos/MiJoe-macOS-arm64/bin/joe
python3 tools/test_macos_native_view.py
python3 tools/generate_vim_help.py --check
python3 tools/verify_pe_xp.py
```

`tools/build_vc6_wine.py --smoke` 新增 `status-path` 项；可用重复的 `--case 名称` 选择回归项。

## 同步核对的现有行为

Right Margin 设置为 12、开启 Word Wrap 后，Vim Insert 输入 `one two three four five six` 会按空白硬换行；
已有长行和无空白的连续长字符串保持不变。本轮没有增加显示层软折行，也没有重排用户文件。
实测记录：[check-right-margin.log](../macos/verification/status-path-20261103/check-right-margin.log)。

当前 Vim Ex 分派尚未实现替换：`:s/foo/bar/`、`:%s/foo/bar/g`、`:%s/foo/bar/gc` 均返回
`Command not implemented`，文件保持原样。这是本轮核对结论，未增加替换功能。
实测记录：[check-substitute.log](../macos/verification/status-path-20261103/check-substitute.log)。
