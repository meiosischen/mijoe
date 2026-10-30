# synhill 配色

`synhill` 是 MiJoe 可选主题，名称按本项目的约定拼写。参考
[Bisqwit 的 Synhili 页面](https://bisqwit.iki.fi/source/synhili.html)，
采用其中 **JOE/Xterm** 示例的颜色，不把 DOS 的 16 色文字描述转换为另一套 RGB。

来源为作者 Git 仓库 `git://bisqwit.iki.fi/synhili.git` 的 `basic_vm.jsf`，
版本 `5aab268cd76e0190095fb701e7bff72b3b4bff09`（1.0.3.3），以及页面中
[JOE/Xterm 截图](https://bisqwit.iki.fi/jutut/kuvat/programming_examples/gwbasictetris4.png)。
`fg_RGB` 使用 Xterm 六级色立方体，每一通道依次为 0、95、135、175、215、255。

| 用途 | RGB | 原始分类 |
| --- | --- | --- |
| 编辑区背景 | `#000000` | Background bg_000 |
| 空白/普通字符 | `#878787` | Background fg_222 |
| 标识符 | `#AFAFAF` | Identifier fg_333 |
| 注释 | `#D75F5F` | Comment fg_411 |
| 关键字 | `#FFFFD7` | Keyword fg_554 |
| 数字 | `#AF5FFF` | Numeric fg_315 |
| 数字后缀 | `#D75FFF` | NumericSuffix fg_415 |
| 字符串内容 | `#00AFFF` | StringContent fg_035 |
| 字符串分隔符/转义 | `#0087D7` | String fg_024 |
| 运算符 | `#00AFAF` | Special fg_033 |
| 括号/分号 | `#00CD00` | Special2 green |
| 标签/行号 | `#AF00AF` | LineNumber fg_303 |
| DATA | `#FFFF87` | Data fg_552 |
| DATA 分隔符 | `#AF875F` | DataQuote/DataDelim fg_321 |
| 错误文字/背景 | `#FF0000` / `#FFD787` | InvalidNumber/Mystery |
| 文件编号 | `#5FD7FF` | 网页截图；Git 版本已改为 fg_415 |
| 状态栏、菜单、选区、光标 | `#000000` / `#BEBEBE` | 截图状态栏黑字灰底；其他界面部件复用此配色 |

MiJoe 沿用各语言已有的语法分类：类型、预处理指令等映射为关键字；数字常量映射为
Numeric；括号映射为 Special2。没有拆分字符串内容与引号的语法文件会将整个字符串
显示为内容色。主题不改变各语言的词法识别规则，也不引入 BASIC 语法解析器。

真彩色终端、Windows GUI 和 macOS 原生窗口使用表中的 RGB。256 色终端使用相同的
色立方体值；ANSI 绿色依赖终端调色板，界面灰色用最接近的 250 号色（`#BCBCBC`）。
16 色终端使用近似颜色，无法精确表示上述 RGB。

默认选择顺序为：显式 `-colors`/配置 → 已保存的终端主题 → `teasp`。
原来的 `default` 主题仍可选择；资源缺失时也用作兼容回退。

编辑器中执行 `:colors synhill` 应用主题，`:colors` 打开主题列表。
旧安装若已记住其他主题，选择一次 `synhill` 并正常退出即可记住新的选择。
