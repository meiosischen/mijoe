# Vim 基础操作

默认启用 Vim 操作，启动处于 Normal 模式。`joe -novim`（或配置中的
`--vim`）使用传统 JOE 键位；Vim 命令 `:set novim` 可关闭。

公共实现位于 `joe/vim.c`，Windows XP/VC6 与 UNIX 共用。
顶部状态栏用 `[N]`、`[I]`、`[V]` 分别表示 Normal、Insert、Visual；按字符、按行及矩形选择统一显示 `[V]`。

- 移动：`h j k l`、方向键、`w b e`、`0 ^ $`、`gg G`、`数字G`、`Ctrl+u/d`。
- 行内定位：`f字符` 向右、`F字符` 向左到目标；`t字符` / `T字符` 停在目标前/后。
  `;` 按原方向重复，`,` 反向重复；如 `2fx`、`3;`。查找限于当前行，支持中文目标。
- 配对跳转：`%` 匹配 `()[]{}`，支持嵌套和跨行；光标位于 C 注释分隔符时匹配 `/* */`，
  条件编译行支持 `#if/#ifdef/#ifndef/#elif/#else/#endif`。`50%`、`100%` 按文件比例跳转。
- 插入：`i a I A o O`；`Esc` 或 `Ctrl+[` 返回 Normal。
- 编辑：`x dd dw d$ cc cw yy yw p P`；次数可以出现在操作符前后，如 `2d3w`。
- 撤销：`u`；重做：`Ctrl+r`；重复修改：`.`。
- 选择：`v` 按字符、`V` 按行，移动扩展选区，`y d c` 操作，`Esc` 取消。
- 列插入：`Ctrl+V` 进入矩形选择，方向键或 `h/j/k/l` 调整；`I` 在左边界插入，
  `A` 在右边界追加，输入后按 Esc 应用到所选各行。`u` / `Ctrl+r` 整批撤销、重做。
- 搜索：`/表达式`、`?表达式`、`n N`，使用 JOE 的标准正则表达式引擎。
- 帮助：`:help` / `:h` 列出本版本支持的命令及简介，PageDown/PageUp 翻页，`q` / Esc 关闭。
- JOE 菜单：`Ctrl+K H` 打开原始帮助，PageDown/PageUp 切页，再次按键关闭；
  `Ctrl+T` 打开选项菜单。`Ctrl+K T` 沿用原绑定，用于缩小窗口。
- 主题：`:colors` / `:colorscheme` 显示选择菜单，方向键选项、PageDown/PageUp 翻页、Enter 应用、Ctrl+C 取消；
  `:colors gruvbox` 可直接切换。原有入口是 `Ctrl+T` → `S Color scheme` → 输入名称 → Enter，Tab 可显示候选。
- 文件：`:e 路径`、`:enew`、`:ls`、`:bn`、`:bp`、`:b 编号`、`:bd[!]`。
- 写盘：`:w`、`:w 新路径`、`:saveas 新路径`，`!` 显式允许覆盖。
- 退出：`:q`、`:q!`、`:wq`、`:x`；写盘失败不会退出或清除修改状态。

已命名缓冲区的 `:w 新路径` 写出副本，`:saveas 新路径` 切换当前文件名。
路径可以含空格，也可在整个路径两端加双引号；Windows 反斜杠保持原意。
缓冲区编号在同一运行会话内稳定。切换文件保留未保存内容；`:bd` 和退出保护修改。
内部寄存器独立于系统剪贴板。Windows Insert 模式支持 Shift+Insert 粘贴。
编码和换行沿用 JOE 的文件选项，已有文件不会因进入 Vim 模式而主动转换。

上述定位可用于 Visual 和 `d/c/y`，例如 `dfx`、`dFx`、`ctx`、`y%`、`v%d`；
次数可组合，如 `2d3fx`。`f/t` 的操作范围包含向右终点，`F/T` 向左时不包含原光标字符；
`d%` 包含括号两端，条件编译配对与数字百分比按整行操作。查找失败会取消待执行操作。
括号匹配按 Vim 的默认转义规则区分奇偶反斜杠，并跳过双引号和 C 单字符常量中的括号。
行为参考 [Vim motion 文档](https://vimhelp.org/motion.txt.html)，通过本机 Vim 做结果对照；
不包含自定义 `matchpairs`、matchit 插件或语言语法树匹配。

例如在行首执行 `Ctrl+V`、`2j`、`I`，输入 `// ` 后按 Esc，可给三行加相同前缀。
输入期间先显示首行修改，按 Esc 后应用到其余行。`Ctrl+V`、`2j`、`$`、`A` 则在每行行尾追加。
矩形按屏幕列计算：`I` 跳过没有到达插入列的短行，`A` 对短行补空格，插入位置落在 Tab 内时拆为空格。
支持中文输入和单行粘贴；输入期间移动光标、删除到原文或输入换行时，后续只保留首行的普通编辑。
再次按 `Ctrl+V` 或按 Esc 取消矩形选择，`v/V` 可切换为字符/整行选择。
行为参考 [Vim blockwise operators](https://vimhelp.org/visual.txt.html#blockwise-operators)。

Vim 模式保留 `Ctrl+K` 前缀绑定和 `Ctrl+T` 的 JOE 路径，菜单和提示框使用原有键位。
主题即时生效，跨会话保留沿用 JOE 的 `joe_state`；macOS 双击测试启动器关闭了状态写入，
重新启动不会记住本次主题选择。启动时固定主题可使用 `bin/mijoe -colors gruvbox 文件路径`。
界面中的中文支持列表源于 `docs/vim-help.txt`；修改后运行
`python3 tools/generate_vim_help.py` 更新供 VC6/UNIX 共用的嵌入头文件。

矩形删除、修改、复制粘贴、命名寄存器、文本对象、Vimscript、完整 Ex 命令和强制重新加载
暂未实现。矩形模式请先使用 `I/A` 再输入或粘贴；未实现的命令会显示提示。

## UNIX 构建

需要 C 编译器、make、Autoconf、Automake，以及可选的 terminfo/termcap 开发库。

```sh
sh autojoe
mkdir -p build/unix
cd build/unix
../../configure --prefix="$PWD/install"
make -j4
make install
./install/bin/joe
```

Windows XP 构建仍使用 `windows/vc6/build.bat Release package`。
各平台测试结果分别记录，Wine 运行不能替代 XP 实机或虚拟机验收。
