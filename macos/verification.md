# macOS 本机版本验证

2026-10-30 翻页快捷键：按用户要求将 `Ctrl+U` 改为整页上翻，新增 `Ctrl+Shift+U`（大写 U）整页下翻，
分别等同 PageUp/PageDown，在 Normal/Insert/Visual 可用；`Ctrl+D` 保留原半页下翻。
macOS 和 Windows 窗口区分 Shift，TUI 接受 CSI-u / modifyOtherKeys 独立编码；传统终端若都发送
ASCII 21 则无法区分，可用 PageDown。`:help` 和操作说明已更新。
21 组真实 PTY 翻页对照、93 项原生检查、46 项终端回归和 32 项 UI 回归通过。
窗口版已安装，实际 Ctrl+U 从第 88 行上翻至 34 行，Ctrl+Shift+U 下翻回 88 行，随后恢复原光标列。
已核验源码、构建包与安装版一致，签名完整性通过；TUI 的 mj 软链已刷新。
详见 [本轮记录](verification/paging-20261030/verification.json)。

2026-10-30 文本对象：补齐 `iw/aw/iW/aW`、`i/a` 搭配 `() [] {} <>`、`b/B` 别名及三种引号。
Visual 与 `d/c/y` 共用范围计算；对齐默认 Vim 的次数、连续/反向扩选、嵌套、转义引号、空白、
标点、中英文边界、跨行及空对象行为，同时修正跨行字符寄存器粘贴后的光标位置。
1032 组本机真实 Vim 对照全部通过，每组还用后续插入标记验证操作后的光标位置；
87 项原生窗口检查，以及原有移动、寄存器、宏、列插入、PTY 和 UI 回归通过。
`:help` 和操作说明已列出这些对象；句子、段落、标签对象及自定义 Vim 字符分类选项尚未提供。
TUI 的 `/usr/local/bin/mj` 已刷新，构建版 `MiJoe.app` 已更新并通过源码哈希、签名校验。
VC6 Release/Debug 的独立版和 DLL 版均构建成功，6 个 PE 文件通过 XP 静态兼容检查。
随后确认旧 App 已是干净空白会话，正常退出并安装新版，安装后二进制与构建包一致，签名校验通过。
实际窗口从 `alpha` 中间执行 `viw`，截图确认完整单词高亮；`y` 后用 `: Ctrl+R 0` 回显 `:alpha`。
旧版窗口未加载修复正是用户再次看到差异的原因；本轮未验证真实 XP 运行。
详见 [本轮记录](verification/text-objects-20261030/verification.json)、[逐项对照](verification/text-objects-20261030/comparison.json)。

2026-10-30 原生窗口绘制优化：光标和内容按受影响单元刷新，缓存字形/颜色，合并滚动请求，
保留触控板小数增量；完整的编辑器重绘帧到齐后再提交终端模型，避免显示中间画面。
84 项原生检查和 95 组 Vim 移动对照通过；桥接、宏、寄存器、grep、大文件、PTY、UI 回归通过。
同一完整终端模型的离屏绘制中，旧循环副本约 15.8ms，新循环约 11.0ms，光标重绘约 1.30ms，
脏区域约为窗口的 5.8%。这些是 CPU 绘制成本，不代表屏幕帧率或实际按键延迟。
早期基线的整屏画面尚未完成预热，其整屏数值不用于比较。
构建包已完成且 TUI 的 `/usr/local/bin/mj` 软链指向最新产物；初次因未保存会话未替换窗口版，
后续在文本对象复核时正常退出并安装，新窗口实际选词检查通过，实际滚动手感仍需用户试用。
本轮渲染优化未重跑 Windows 构建或 XP 实机；后续文本对象构建验证见上。
用户报告的 `ft` 暂未复现，增加了分次按键、后端位置和可见光标的检查，未声称修复未定位的原因。
详见 [验证记录](verification/native-render-20261030/verification.json)。

2026-10-28 原生窗口版：新增 `MiJoe.app`，已安装至 `~/Applications/MiJoe.app` 并启动。
AppKit 独立窗口、原生菜单和保存面板、中文粘贴显示、修改标记及关闭确认已通过实际界面检查。
218 项编辑/控制通道回归、10 项离屏 AppKit 输入客户端与会话检查通过；
不启用 macOS 包扩展的普通 UNIX 版本重新构建并通过 45 项终端回归。
安装目录的 115 个文件与构建包一致；ZIP 实际解压后逐文件一致，程序可执行权限和签名完整性检查通过。
真实输入法候选框、Intel 和旧版 macOS 尚未验证；本轮没有重跑 Windows 构建。
详见 [原生版清单与日志](verification/native-app-20261028/manifest.json)、[使用说明](README.md)。

2026-10-28 picture mode / 菜单修复：Vim Normal 的 `h/j/k/l` 和方向键、Insert 的方向键
沿用 JOE 的屏幕列移动，允许越过行尾并在输入时补空格；短行保留目标列，末尾扩展可撤销且遵守只读保护。
撤销、整行修改和矩形插入会同步实际编辑列。Vim 选项菜单中再次按 `Ctrl+T` 关闭菜单，
不再误选 `T Overtype`；显式 `T/t` 切换保留，传统 `-novim` 菜单键位沿用原行为。
最终原生检查共 205 项通过：48 项包回归、84 项定位/保护、42 项列插入/保护、31 项 UI。
已更新 `/usr/local/libexec/mijoe` 及 `mj/rmj/xmj`，实际系统 `mj` 的 5 项专项 PTY 检查通过。
本轮构建、用例日志、源码/二进制哈希及安装结果见 [记录](verification/picture-menu-20261028/manifest.json)。

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

## synhill 默认主题（2026-10-29）

按 Synhili 的 JOE/Xterm 原始配色增加 `synhill`；真彩色/RGB、256 色和 16 色回退均有定义。
macOS 终端界面 32 项、原生 AppKit 19 项检查通过，包括实际屏幕单元的 RGB、主题切换和文件保护。
本机 `~/Applications/MiJoe.app` 的 116 个文件与构建包一致，签名校验通过；现有 `mj` 通过个人主题目录及
保存的终端配色设置启用 `synhill`，本轮未替换 `/usr/local/libexec/mijoe` 的二进制。
原有个人主题设置备份为 `~/.joe_state.before-synhill-20261029`。

VC6 Release/Debug 的独立/DLL 版均构建成功，4 份内置主题解压后与源文件一致，Release 的 50 项
Wine 回归及 6 个 PE 文件的 XP 静态兼容检查通过。XP 实机、现代 Visual Studio 未验证。
详见 [本轮验证清单](verification/synhill-20261029/verification.json) 与
[原生窗口检查](verification/synhill-20261029/native-view.json)。


## TUI 构建与 mj 软链（2026-10-29）

`/usr/local/bin/mj` 已设置为指向 `build/macos/MiJoe-macOS-arm64/bin/joe` 的软链。
TUI 构建和原生 AppKit 构建（包括 `--skip-core`）默认都校验此链接；首次需要管理员身份创建，
之后目标文件原子更新，正确的链接不需再写系统目录。链接无法校验时构建明确失败。
`--no-command-link` 是显式只打包选项。软链方式需要保留仓库的 build 目录。

5 项链接测试通过，普通用户完整重编译和 AppKit 复用核心构建通过；直接通过真实系统路径
验证了资源定位、带空格的文件名、搜索高亮、`:noh`、编辑和保存。
详见 [本轮验证记录](verification/command-link-20261029/verification.json)。

## 大文件只读懒加载（2026-10-29）

新增 15 项真实编辑器测试通过：默认阈值下的 513 MiB 实际文本文件、
5 GiB+17 字节稀疏二进制文件、首尾与百分比跳转、跨区域及跨 64 KiB 搜索、
UTF-16 转码和完整单词搜索、只读与保存防护、取消返回原修改缓冲区、
命令行只读/HEX 参数保留。打开前只采样 64 KiB，首尾定位只读取相邻窗口。
已有 46 项 PTY、13 项原生控制桥、32 项界面、47 项 AppKit 视图检查及 Vim 动作对照通过。
VC6 Debug/Release 的独立版与 DLL 版构建通过，6 个 PE 文件的 XP 静态兼容检查通过。
XP 实机未测试；原生大文件选择框已编译但桌面工具不可访问，尚未完成实际点击验证。
新版 App 保存在构建目录；已安装旧版仍在运行，未替换其 bundle。`/usr/local/bin/mj` 已更新。
详见 [本轮验证记录](verification/large-files-20261029/verification.json) 和 [功能边界](../docs/large-files.md)。

## Cyan 命令行、三模式移动、当前文件 grep（2026-10-30）

`:command` 输入行使用亮 cyan，取消 DIM/italic，保留消息背景。Normal/Insert/Visual
支持 Ctrl+A/E、Ctrl+P/N、Ctrl+B/F，Ctrl+K 前缀仍交给 JOE。新增当前文件 `:grep 表达式`，
结果列表居中、约占半屏面积，显示行号与内容，支持上下键、Ctrl+P/N、分页、预览、
Enter 确认、Esc 恢复原位置，随 resize 调整；弹窗期间屏蔽底层编辑、粘贴和文件操作。
普通文件支持 JOE 正则；懒加载文件使用有界内存流式字面匹配，最多 10000 个匹配行。

12 项专项测试通过（含 18 个三模式快捷键子用例）、55 项 AppKit 视图检查、46 项 PTY、
32 项 UI、15 项大文件、13 项控制桥，以及 7 项动作测试（含 90 个 Vim 对照子用例）通过。
VC6 Debug/Release 的独立版、DLL 版构建和 6 个 PE 的 XP 静态兼容检查通过；XP 实机未验证。
已实际操作原生窗口验证 cyan、中文结果、Ctrl+N/箭头/G 定位滚动、Enter 和 Esc。
本机 `~/Applications/MiJoe.app` 与 `/usr/local/bin/mj` 均已更新，字体 14、行距 -5 保留，
`.txt`、`.log` 默认打开关联复查有效。
详见 [验证记录](verification/grep-controls-20261030/verification.json) 和 [原生结果窗口](verification/grep-controls-20261030/grep-modal.png)。

## grep 光标稳定与 Vim 寄存器（2026-10-30）

去掉 grep 每次刷新时的空白擦除和整块 dirty 标记，保留屏幕缓存的差异绘制。
弹窗期间隐藏文本光标、以选择行定位，关闭时恢复。重复状态刷新不会重画整个列表。
加入未命名、0、1–9、-、a–z/A–Z 追加和黑洞寄存器，命令栏与 Insert 可用 Ctrl+R 插入。
Visual、JOE 标记区域和原生复制均接入复制寄存器；普通模式 Ctrl+R 仍是重做。

14 项寄存器/稳定性测试通过；61 项原生视图、12 项 grep/控制键、46 项 PTY、32 项 UI、
15 项大文件、13 项控制桥及动作对照通过。命名复制/追加行为与本机 Vim 对照。
VC6 Debug/Release 独立版与 DLL 版构建和 6 个 PE 的 XP 静态兼容检查通过；XP 实机未验证。
本机 App 与 mj 均已更新；实际操作已验证两种选择方式的 Ctrl+R 0 和无可见文本光标的 grep 列表。
详见 [验证记录](verification/registers-cursor-20261030/verification.json)。

## 寄存器回显、查看与 F5/F6 宏（2026-10-30）

双引号及寄存器名称回显待执行状态；`:reg`/`:registers` 可分页查看类型、字节数、
转义后的内容，并支持 `:reg a` 等名称过滤，每项预览上限 16 KiB。
F5 开始/结束键盘录制，状态栏显示 REC；F6 通过原键盘分发路径回放最近宏。
`:macro`/`:macros` 展示状态、起始模式、键数及可读按键序列。
中文、粘贴、Ex 输入、方向键与 JOE Ctrl+K 组合键均测试；Ctrl+C 中断、错误停止。
宏只保存在当前进程，回放要求与录制开始相同模式；不录制鼠标或原生菜单操作。

12 项宏/查看专项、68 项原生视图、14 项寄存器、12 项 grep/控制键、46 项 PTY、
32 项 UI、13 项控制桥、15 项大文件及动作对照通过。
VC6 Debug/Release 独立版和 DLL 版均构建通过，6 个 PE 的 XP 静态兼容检查通过；XP 实机未测。
本机 App 与 mj 已更新；实际按 F5 录制 I// <Esc>j，再按 F6 给下一行加注释，
并通过 :macro 确认六个按键。样例修改已撤销，恢复空白窗口，字体 14、行距 -5 保留。
详见 [验证记录](verification/register-inspection-macros-20261030/verification.json)。
