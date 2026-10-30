# macOS 版本

## 原生独立窗口

```sh
python3 tools/build_macos_app.py --install
```

构建原生 AppKit 应用并安装到 `~/Applications/MiJoe.app`。双击应用即可打开独立窗口，
可将应用拖到 Dock；在 Finder 中可通过“打开方式”选择 MiJoe。
构建目录的应用为 `build/macos/MiJoe.app`，分发包为 `build/deliveries/mijoe-native-macos-arm64.zip`。
构建或更新安装前，请退出对应路径下已经运行的 MiJoe。

- `⌘N` 新建窗口，`⌘O` 原生文件打开框，支持同时打开多个文件。
- `⌘S` 保存，未命名缓冲区显示原生保存框；`⇧⌘S` 另存为。
- `⌘Z` / `⇧⌘Z` 撤销/重做，`⌘A` 全选，`⌘C/X/V` 复制/剪切/粘贴。
- `⌘W` 关闭窗口，`⌘Q` 退出；存在未保存缓冲区时提示保存、放弃或取消。
- `⌘+` / `⌘-` 调整字体，`⌘0` 恢复默认大小；“显示”菜单打开主题与选项。
- `F1/F2` 增大/减小字号 1 点，`F3/F4` 增大/减小行距 1 像素，立即重排并刷新窗口。
  默认字号 13 点，行高为字体标准行高减 6 像素；设置会记住。若功能键被系统占用，可按 `Fn+F1` 等。
- 鼠标点击定位、拖动选择、滚轮滚动；窗口缩放和全屏会调整编辑区域。
- “缓冲区”菜单切换当前编辑会话中的缓冲区；Vim 的 `:e/:bn/:bp` 等命令继续可用。

teasp 的选区和搜索匹配使用浅白底色，光标为绿色实心块；`/`、`?`、`n/N` 搜索持续高亮，`:noh` 清除。

右上角状态显示 `R` 行号、`C` 列号、`FS` 字号、`H` 行距（相对字体标准行高的像素偏移），F1–F4 调整后实时更新。

默认使用 Vim 模式及 teasp 主题，顶部 `[N/I/V]` 显示模式；`i` 开始输入，Esc 返回。
“帮助”菜单列出支持的编辑命令，原有 `Ctrl+T`、`Ctrl+K H`、`:help`、`:colors` 继续可用。
中文输入通过 `NSTextInputClient` 接入系统输入法，候选定位使用当前光标的屏幕坐标；
组合中的文字在提交前保留在窗口层。字体大小保存在应用自己的偏好设置中。

每个窗口对应一个独立编辑会话，可以在其中使用多个缓冲区。关闭时检查该会话的全部未保存缓冲区；
“保存”写入当前缓冲区，其他缓冲区仍有修改时保留窗口，需切换后逐一保存。
“放弃更改”会丢弃该窗口内所有未保存修改。关闭全部窗口后应用保留在 Dock，`⌘N` 可再次新建。

AppKit 直接绘制字符网格和输入法组合文字。编辑内核作为应用内部子进程运行，通过 PTY 输出界面，
原生菜单通过独立控制管道访问文件、选择区和修改状态。保存路径按原始 UTF-8 数据传递，
文件名中的引号或换行不会作为命令执行。应用从自己的 bundle 读取程序、配置、语法和主题资源。
使用固定版本的 [libvterm 0.3.3](https://www.leonerd.org.uk/code/libvterm/) 解码显示数据，源码和 MIT 许可证随仓库提供。

此应用声明最低 macOS 11，实际验收以本机 ARM64/macOS 26.3.1 为准；Intel 和旧系统仍需验证。
原生版与终端版共用编辑内核。`mj/rmj/xmj` 继续使用已安装的终端版。

## 终端版

在 macOS 上运行（编译成功后自动校验 `/usr/local/bin/mj` 指向本次 TUI 产物）：

```sh
python3 tools/build_macos.py
```

需要 Xcode 命令行工具、Autoconf 和 Automake。构建和安装过程都在仓库的
`build/macos/` 下执行，运行产物仅依赖 macOS 系统库。

本机 Apple Silicon 产物：

- 双击 `build/macos/MiJoe-macOS-arm64/启动MiJoe.command`，在终端中打开练习文件。
- 终端运行 `build/macos/MiJoe-macOS-arm64/bin/mijoe 文件路径`。
- 可分发包：`build/deliveries/mijoe-vim-macos-arm64.zip`。

Intel Mac 上执行同一脚本会构建本机的 `x86_64` 版本；未做跨架构编译或验收。
本产品使用系统终端承载编辑界面。

默认启用 Normal、Insert、Visual 和 `:` 命令。退出用 `Esc`、`:q`、回车，
保存退出用 `:wq`；`-novim` 保留传统 JOE 操作。详见 [操作说明](../docs/vim.md)。

`:help` 查看已支持命令，PageDown/PageUp 翻页，q/Esc 关闭。
`:colors` 直接显示主题菜单；也可按 Ctrl+T、S，输入主题名并回车。
Ctrl+K H 打开 JOE 原始帮助；Ctrl+K T 是缩小窗口。
双击测试入口关闭了 joe_state 写入，重新启动不会记住本次主题选择。

列插入：行首按 Ctrl+V、2j、I，输入前缀后按 Esc，即给三行插入相同前缀；
A 在矩形右侧追加，$A 在每行行尾追加。u / Ctrl+r 整批撤销、重做。

`bin/joe` 通过实际可执行文件位置查找相邻 `etc/joe/` 和 `share/joe/`，
与当前工作目录和开发者机器路径无关。移动时保留整个目录结构。
可执行文件经符号链接启动时仍以真实程序位置查找资源。

双击入口关闭状态文件写入，重复构建保留用户已编辑的练习文件。
用户原有 `~/.joerc` 的读取规则沿用 JOE；启动器不修改它。

本机回归测试：

```sh
python3 tests/macos_package_test.py build/macos/MiJoe-macOS-arm64
python3 tests/vim_motion_test.py build/macos/MiJoe-macOS-arm64/bin/joe
python3 tests/vim_ui_test.py build/macos/MiJoe-macOS-arm64/bin/joe
python3 tests/vim_block_test.py build/macos/MiJoe-macOS-arm64/bin/joe
```

定位与列插入对照测试需要本机 `vim`，逐项比较实际编辑结果。
程序更新后退出已有编辑器，再重新启动即可加载新版。

首次设置 `mj` 软链（先完成上述构建，再从仓库根目录执行一次）：

```sh
sudo python3 tools/link_macos_command.py
```

`/usr/local/bin/mj` 固定指向当前架构的 `build/macos/MiJoe-macOS-arm64/bin/joe`（Intel 为 x86_64）。
构建会原子替换目标文件，所以之后每次成功编译，`mj` 自动使用最新版；正确的链接不需要再次写系统目录。
首次无管理员权限时，构建会明确报错并输出设置命令，不会把未更新的入口报告为成功。
只打便携包而不设置系统命令，可显式使用 `python3 tools/build_macos.py --no-command-link`。
软链方式需要保留此仓库的 build 目录。原生 AppKit 构建默认也会运行上述 TUI 构建和链接校验。

也可安装三种系统入口（该命令同时设置上述 `mj` 软链）：

```sh
sudo python3 tools/install_macos.py
```

安装程序将独立的程序与配置、主题、语法资源复制至 `/usr/local/libexec/mijoe`，
在 `/usr/local/bin` 创建以下入口：

- `mj 文件`：软链到本仓库的最新 TUI 产物，默认 Vim 操作。
- `rmj 文件`：运行独立安装副本，用 JOE 的 `-rdonly` 选项只读打开。
- `xmj 文件`：运行独立安装副本，用 `-hex -novim` 进入 JOE 原生十六进制编辑；Ctrl+K X 保存退出，Ctrl+K Q 退出。

三个入口保留调用者的工作目录和文件名参数。系统目录需要管理员权限，
安装器不会覆盖同名的无关命令。`mj` 随构建自动更新；`rmj/xmj` 的独立副本用该安装器更新。

本轮结果见 [验证记录](verification.md)。

## 大文件只读懒加载

超过 512 MiB 的文本或二进制文件先选择“只读懒加载 / 正常打开 / 取消”；
二进制文件自动进入 HEX。懒加载使用约 1 MiB 原文件窗口和 64 位偏移，
不允许编辑或保存。`mj -lazy 文件` 可直接强制懒加载。
搜索和按行号定位按需扫描，Esc 取消；一般正则、全文件全选和同缓冲区分屏不可用。
具体行为见 [大文件说明](../docs/large-files.md)。

原生窗口的 F5/F6 现分别用于键盘宏录制/停止与回放；F1～F4 仍调整字号/行距。
`:reg [寄存器名]` 查看复制内容，`:macro` 查看最近宏；查看窗口支持分页，q/Esc 返回。
