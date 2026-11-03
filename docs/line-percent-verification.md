# 右上角行位置百分比验证

日期：2026-11-03。

右上角 `R / C` 后、`FS / H` 前增加整数百分比，计算为当前逻辑行数除以全部逻辑行数再乘 100，向下取整。
末尾换行的内部空行不计入总数，空文件为 100%，插入模式在末尾的虚拟行钳制到 100%。
软折行不增加逻辑行数；Hex 按 16 字节的显示行计算。

文本懒加载只在当前加载窗口已经覆盖整个源文件时使用精确行数，其余显示 `--%`。
状态读取不扫描文件、不增加懒加载读取量；既有 `RO-LAZY` 字节进度仍保留，二者明确区分。
Hex 懒加载利用已知源文件大小计算，无需扫描换行。

| 验证 | 结果 |
| --- | --- |
| 实际编辑器百分比 | 11 项通过，含不等长行、无末尾换行、空行、编辑撤销、Visual/Insert、折行、Hex 及懒加载读取量 |
| AppKit 百分比显示 | 16 项通过；实际顶栏展示 25/50/100%，折行翻页与缩放后仍正确 |
| 路径 / 大文件 / 软折行 / recent | 11 / 15 / 11 / 20 项通过 |
| 原生窗口 / 折行 / 替换确认 | 121 / 42 / 20 项通过 |
| VC6 Release/Debug × 独立/DLL | 四种构建成功，每种 5 项 Wine 界面回归通过，共 20 项 |
| XP PE 静态检查 | 六个 PE32/i386 文件通过架构、子系统和已知 API/运行库导入检查 |
| 本机安装 | 前后端哈希、源码清单及代码签名核对通过；`mj` 软链指向最新 TUI |

已更新 `~/Applications/MiJoe.app` 并重新启动，实际窗口显示 `R 1 C 1 100% FS 14 H -4`，保留个人字号和行距。
Windows 动态检查在 Wine 中运行，未在真实 Windows XP 上执行。

证据：[macOS](../macos/verification/line-percent-20261103/)、
[VC6/Wine 与 PE](../windows/vc6/verification/line-percent-20261103/)。

```sh
python3 tests/line_percent_test.py build/macos/MiJoe-macOS-arm64/bin/joe
python3 tools/test_macos_native_view.py --line-percent
python3 tools/generate_vim_help.py --check
python3 tools/verify_pe_xp.py
```
