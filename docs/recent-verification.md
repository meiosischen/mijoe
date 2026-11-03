# 个人配置与最近文件验证

日期：2026-11-03。

个人配置迁入 `~/.mj`；`:recent` 保存最近 200 个文件，按最近打开顺序排列。
弹窗对长路径折行，支持滚动条、方向键、Ctrl+P/N、PageUp/PageDown、鼠标和滚轮。
打开时还原只读、文本/Hex、懒加载和软折行模式；缺失文件报错并从历史删除。
保存、另存为和模式切换更新历史，多窗口通过文件锁和原子替换合并更新。
旧个人配置复制迁移，不覆盖新配置；字体、行距及窗口配置使用平台对应的新文件。
配置布局、历史格式和使用方法见 [个人配置与最近文件](personal-profile.md)。

## 验证结果

| 项目 | 结果 |
| --- | --- |
| macOS TUI / AppKit 构建 | 成功；源码清单哈希、已安装前后端哈希及代码签名核验通过 |
| 最近文件 / 个人配置 PTY 测试 | 20 项通过，含 200 条上限、六进程更新、迁移、错误处理、鼠标、懒加载及模式恢复 |
| AppKit 最近文件集成 | 54 项通过，含长 Unicode 路径、分页、滚动条、模态保护、缩放重绘、光标恢复与字体配置 |
| 既有 AppKit / 软折行集成 | 121 / 42 项通过 |
| 软折行 / 状态栏路径 / Vim / bridge / UI | 11 / 11 / 46 / 13 / 32 项通过 |
| grep 控制 / 退出 / 大文件 / 内部管道 | 14 / 8 / 15 / 41 项通过 |
| 寄存器 / 缓冲区列表 / grep 导出 | 14 / 8 / 16 项通过 |
| VC6 Release/Debug × 独立/DLL | 四种构建成功，每种 8 项动态检查，共 32 项通过 |
| Windows 字体、行距与配置恢复 | 两种 Release 各 32 项检查通过，共 64 项；测试使用独立配置目录 |
| XP PE 静态检查 | 六个 PE32/i386 文件，子系统不高于 5.1，未引入检查名单内的 XP 之后 API 或外部 VC CRT |

Windows 动态验证在 Wine 上完成，未在真实 Windows XP 上运行。

最新 App 已安装到 `~/Applications/MiJoe.app`；`/usr/local/bin/mj` 软链指向本仓库的最新 TUI 构建。
已安装版本同时包含路径选择/双击复制修复和默认关闭的独立软折行开关。

证据：[macOS 日志、构建清单与截图](../macos/verification/recent-20261103/)、
[VC6/Wine、字体及 PE 检查](../windows/vc6/verification/recent-20261103/)。

```sh
python3 tests/recent_test.py build/macos/MiJoe-macOS-arm64/bin/joe
python3 tools/test_macos_native_view.py --recent
python3 tools/test_macos_native_view.py --softwrap
python3 tools/generate_vim_help.py --check
python3 tools/verify_pe_xp.py
```
