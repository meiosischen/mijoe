# macOS 本机测试版

在 macOS 上运行：

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

安装为系统命令（先完成上述构建，再从仓库根目录执行）：

```sh
sudo python3 tools/install_macos.py
```

安装程序将独立的程序与配置、主题、语法资源复制至 `/usr/local/libexec/mijoe`，
在 `/usr/local/bin` 创建以下入口，不依赖仓库的 build 目录：

- `mj 文件`：默认 Vim 操作。
- `rmj 文件`：用 JOE 的 `-rdonly` 选项只读打开。
- `xmj 文件`：用 `-hex -novim` 进入 JOE 原生十六进制编辑；Ctrl+K X 保存退出，Ctrl+K Q 退出。

三个入口保留调用者的工作目录和文件名参数。系统目录需要管理员权限，
安装器不会覆盖同名的无关命令。后续构建更新后重新运行安装器即可更新已安装版本。

本轮结果见 [验证记录](verification.md)。
