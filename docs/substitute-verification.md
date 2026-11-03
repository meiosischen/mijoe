# Ex 替换验证

日期：2026-11-03。

支持 `:s` / `:substitute`、当前行/全文/数字及 Visual 行范围、`g/c/i/I/e` 标志、
分组替换、匹配串展开、空搜索模式复用、重复上次替换和整次撤销/重做。
逐项确认持有可随编辑更新的位置标记，阻止背景原生编辑操作；原生状态轮询不会清掉确认提示。
Visual 范围只自动用于替换，保留既有选中内容转 `:grep` / Ctrl+R 寄存器的操作。
只读、Hex 和懒加载文件拒绝修改；不支持的高级 Vim 语法明确报错。

替换使用独立的行内贪婪匹配入口，普通搜索和 grep 保留原匹配行为。
正则线程容量耗尽返回错误，不再直接退出编辑器。
支持范围与限制见 [操作说明](vim.md#ex-文本替换) 和内置 `:help`。

| 验证 | 结果 |
| --- | --- |
| 实际编辑器替换 / 系统 Vim 对照 | 39 项通过；包含分组贪婪、空匹配推进、Unicode、CRLF、插入换行、无末尾换行、范围、确认、撤销及保护 |
| AppKit 替换确认 | 20 项通过，含当前项浅白高亮、提示保留、原生操作保护、确认结果、撤销与 Ctrl+C |
| 既有 AppKit | 121 项通过 |
| Vim / 寄存器 / UI / grep 控制 | 46 / 14 / 32 / 14 项通过 |
| 内部管道 / recent / 软折行 | 41 / 20 / 11 项通过 |
| VC6 Release/Debug × 独立/DLL | 四种构建成功，每种 9 项 Wine 检查通过，共 36 项 |
| XP PE 静态检查 | 六个 PE32/i386 文件通过架构、子系统和已知 API/运行库导入检查 |

退出回归测试采用有截止时间的进程等待，避免把繁忙系统下尚未完成的正常退出误判为失败。
Windows 动态检查使用 Wine，未在真实 Windows XP 上执行。

证据：[macOS](../macos/verification/substitute-20261103/)、
[VC6/Wine 与 PE](../windows/vc6/verification/substitute-20261103/)。

```sh
python3 tests/vim_substitute_test.py build/macos/MiJoe-macOS-arm64/bin/joe
python3 tools/test_macos_native_view.py --substitute
python3 tools/generate_vim_help.py --check
python3 tools/verify_pe_xp.py
```
