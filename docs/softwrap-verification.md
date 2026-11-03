# 独立软折行开关验证

日期：2026-11-03。

- 默认关闭。`Ctrl+T` → `Q Soft wrap` 切换，`:set wrap` / `:set nowrap` 明确开启、关闭。
- 有效宽度是 Right Margin 与窗口正文宽度的较小值。边界、行号、窗口或字号变化会重新布局。
- 只改变显示，不插入换行、不标记修改、不重写文件。原有 Word Wrap 继续独立控制输入时硬换行。
- `j/k`、上下键仍按逻辑行移动；翻页、滚轮与鼠标定位使用折行后的屏幕位置。
- 中文宽字符、组合字符、Tab、搜索高亮、Visual、语法高亮与续行行号均覆盖。
- 每个视图最多缓存两个逻辑行的各 256 个折行位置，不建立文件大小的索引；适配只读懒加载窗口。
- Hex、Picture、隐藏 ANSI 模式和 shell 窗口使用原有显示，软折行开关状态保留。

## 实测

| 项目 | 结果 |
| --- | --- |
| macOS TUI / AppKit 构建 | 成功，源码与构建清单哈希一致；`mj` 软链仍指向本次构建 |
| 实际编辑器软折行测试 | 11 项通过，含默认关闭、独立菜单、既有内容、编辑撤销、鼠标、翻页、宽字符、窗口缩放、超长单行及懒加载 |
| AppKit 离屏软折行集成 | 42 项通过，含真实屏幕字符、光标坐标、语法/选区/搜索颜色和缩放后重绘 |
| 既有路径 / Vim / bridge / UI / grep / 退出 / 大文件 | 11 / 46 / 13 / 32 / 14 / 8 / 15 项通过 |
| 既有 AppKit 集成 | 121 项通过 |
| VC6 Release/Debug × 独立/DLL | 四种构建成功；每种折行后鼠标编辑、路径复制、Visual、复制粘贴、命令行粘贴均通过，共 20 项 |
| Windows XP 静态检查 | 六个 PE32/i386 文件，子系统不高于 5.1，未引入检查名单内的 XP 之后 API 或外部 VC CRT |

证据：[macOS 测试与截图](../macos/verification/softwrap-20261103/)、[VC6/Wine 与 PE 检查](../windows/vc6/verification/softwrap-20261103/)。
Windows 动态测试在 Wine 上完成，未在真实 XP 上运行。
窗口应用已构建到 `build/macos/MiJoe.app`；用户正在运行的 `~/Applications/MiJoe.app` 为旧版，本轮没有强行关闭或替换它。

```sh
python3 tests/softwrap_test.py build/macos/MiJoe-macOS-arm64/bin/joe
python3 tools/test_macos_native_view.py --softwrap
python3 tools/test_macos_native_view.py
python3 tools/generate_vim_help.py --check
python3 tools/verify_pe_xp.py
```
