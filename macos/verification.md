# macOS 本机版本验证

2026-10-28 增量：顶部模式标签简化为 `[N]`、`[I]`、`[V]`，字符/整行/矩形选择均显示 `[V]`。
五种模式切换与实际状态栏显示检查通过，见 [本轮记录](verification/statusbar-20261028.json)。
下方 188 项完整功能检查属于 2026-10-27；本次仅调整显示文案，未重复完整功能回归。

日期：2026-10-27。环境：Apple Silicon / macOS 26.3.1。

## 实现

- 用现有公共 Vim 编辑层构建原生 ARM64 终端程序，启动入口为可双击的 `.command` 文件。
- `joe/macos.c` 根据真实可执行文件位置查找相邻配置、主题、语法和语言资源。
- 通过 `MIJOE_PORTABLE_MACOS` 开关启用资源路径适配，Windows 和普通 UNIX 构建路径保留。
- 构建脚本使用临时 DESTDIR 安装到仓库内的产物目录；不写入 `/usr/local` 等系统目录。
- 命令行入口保留调用者的工作目录；双击入口在包内打开练习文件并关闭 joe_state 写入。
- 重复构建保留已有练习文件，启动器不修改用户的 HOME、shell 配置或 joerc。
- 2026-10-27 增加 `f/F/t/T`、`;`、`,`、`%`、次数和操作符/Visual 组合；公共实现供 Windows 共用。
- 安装时原子替换程序文件，已运行的旧编辑器可继续使用，退出重启后加载新版。
- 增加中文 `:help` 支持列表，按窗口宽度换行、PageDown/PageUp 翻页、q/Esc 关闭；
  恢复 `Ctrl+K` 前缀和 `Ctrl+T` 的原有 JOE 路径，并增加 `:colors[cheme]` 主题选择菜单与按名称切换。
- 增加 `Ctrl+V` 矩形选择及 `I/A` 列插入，按 Esc 同步到所选行；支持 `$A` 按各行行尾追加、
  屏幕列对齐、短行、Tab、中文宽字符、整次撤销/重做和单行粘贴。矩形删除/复制等暂未开放。

## 结果

| 检查 | 结果 |
| --- | --- |
| 原生构建、暂存安装及打包 | 成功，`build/macos/arm64/` 保存完整日志 |
| 文件格式 | Mach-O 64-bit arm64 |
| 动态库依赖 | 仅 `/usr/lib/libncurses.5.4.dylib` 和 `/usr/lib/libSystem.B.dylib` |
| 现有真实终端输入测试 | 45 项通过 |
| 移动到含中文、空格和单引号的目录后读取配置并编辑保存 | 通过 |
| 符号链接启动真实程序后读取随包配置 | 通过 |
| 移动目录后加载包内专用 256 色主题 | 通过，终端输出包含该主题的指定颜色 |
| Vim 定位命令对照 | 78 组实际 Vim 与 MiJoe 编辑结果一致，含 vtV/vfV 的专项检查，涵盖方向、次数、操作符、Visual、中文目标、嵌套、引号、注释、条件编译和百分比 |
| 定位失败和取消保护 | 6 项通过，覆盖缺少第 N 个目标、Esc、无查找历史、未配对括号、无效百分比和后续命令 |
| 帮助、菜单与主题 UI | 14 项真实 PTY 检查通过，覆盖中文列表、翻页、边界、缩放、关闭还原、撤销/粘贴保护、原始帮助、原始选项菜单、Insert 返回、主题选择/取消/错误和实际颜色输出；见 [日志](verification/ui-tests.log) |
| 列插入 Vim 对照与保护 | 37 组实际 Vim 对照及 5 项保护检查通过，覆盖 I/A、反向选择、短行、$A、空行、Tab、中文宽字符、撤销/重做、重复、模式切换、CRLF、无末尾换行、只读和粘贴；见 [日志](verification/block-tests.log)、[逐项结果](verification/block-comparison.json) |
| 合计 | 48 项回归 + 78 组定位对照 + 6 项保护 + 14 项 UI + 37 组列插入对照 + 5 项保护 = 188 项 |
| 本机启动 | 本轮新版由实际 PTY 启动并编辑保存；此前已打开的 Terminal 会话需退出重启后加载新版 |
| 签名完整性 | 本机 `codesign --verify` 通过；不表示已取得发行证书或公证 |
| ZIP | 可解压，包含可执行权限和所需资源；哈希见 [清单](verification/manifest.json) |

## 使用入口

- 双击：`build/macos/MiJoe-macOS-arm64/启动MiJoe.command`。
- 命令行：`build/macos/MiJoe-macOS-arm64/bin/mijoe 文件路径`。
- 可分发包：`build/deliveries/mijoe-vim-macos-arm64.zip`。

默认处于 Normal 模式，`i` 输入、`Esc` 返回，`:w` 保存，`:saveas 文件路径` 另存为，
`:q` 退出，`:wq` 保存退出。操作说明随包提供。

## 复现与边界

```sh
python3 tools/build_macos.py
python3 tests/macos_package_test.py build/macos/MiJoe-macOS-arm64
python3 tests/vim_motion_test.py build/macos/MiJoe-macOS-arm64/bin/joe
python3 tests/vim_ui_test.py build/macos/MiJoe-macOS-arm64/bin/joe
python3 tests/vim_block_test.py build/macos/MiJoe-macOS-arm64/bin/joe
python3 tools/generate_vim_help.py --check
```

只验收了这台 ARM64 Mac；未验证 Intel Mac 或其他 macOS 版本。
中文文本、中文路径和粘贴协议由真实 PTY 测试覆盖；系统输入法候选框操作仍需用户试用。
本轮 GUI 工具禁止读取 Terminal，因此启动证据来自实际终端子进程；未声称完成终端窗口画面检查。
Windows 的 VC6 构建与 Wine 窗口测试另见 [Windows 验证记录](../windows/vc6/vim-verification.md)，
不能替代 XP 实机验收。本轮未提交或推送代码。

## 本机命令安装

2026-10-27 通过 macOS 管理员认证，将程序及资源独立安装到 `/usr/local/libexec/mijoe`，
并创建 `/usr/local/bin/mj`、`rmj`、`xmj` 三个启动脚本。未修改用户 shell 配置。
`mj` 正常编辑，`rmj` 使用 `-rdonly`，`xmj` 使用 `-hex -novim`，以 JOE 原生键位编辑二进制。

实际系统路径的三个命令均完成 PTY 检查：普通编辑保存、只读修改/写盘保护、十六进制字节修改并保存且长度保持。
程序与 107 个资源文件逐一匹配构建包，签名完整性检查通过。记录见 [安装检查](verification/command-install.json)。
后续使用 `sudo python3 tools/install_macos.py` 更新安装；与 `build/` 包分离，删除或移动仓库不会影响已安装程序。

列插入完成后已更新系统安装；使用 `python3 tests/vim_block_test.py /usr/local/bin/mj` 复核实际命令路径，
记录另存为 `verification/installed-block-tests.log`。已运行的旧编辑器需退出后重新启动。
