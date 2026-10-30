# teasp

默认主题 `teasp` 移植自 https://github.com/arnau/teaspoon.vim ，固定版本
`215b4010c4be1c3711f69c780ba4224f6c527096`。主题文件保留原作者的许可声明。
真彩色/Windows GUI 使用原始 RGB；256 色使用原主题 Vim 函数计算的 cterm 色号，
16 色使用原主题的低色彩定义。MiJoe 沿用已有语言语法分类，因此不同编辑器的分类范围可能不同。

| 分类 | 原主题组 | RGB |
| --- | --- | --- |
| 正文/背景 | Normal | #E8E8D3 / #222222 |
| 注释 | Comment | #888888 |
| 标识符 | Identifier | #EFD14E |
| 关键字/运算符 | Statement | #00C7E7 |
| 类型 | Type | #FFB964 |
| 数字常量 | Constant | #FF6666 |
| 字符串 | String | #FAFF8A |
| 选区（本项目调整） | Visual | #222222 / #E8E8E8 |
| 光标（本项目调整） | Cursor | #000000 / #00CC66，实心 |
| 状态栏（本项目调整） | 深色文字/浅色背景 | #222222 / #E8E8D3 |
| 星号（本项目调整） | Function 色 | #FFB454，粗体 |
| WARN | Todo | #FF0066，粗体 |
| ERROR | ErrorMsg | 正文前景 / #FF0066 背景 |

`:colors teasp` 切换主题；`:colors` 显示列表。显式设置和已保存的主题仍优先于程序默认值。

纯文本、`.txt`、`.log`、`.log.*` 使用日志语法，按独立的大写 ASCII 标识符高亮 `WARN` 和 `ERROR`。
`WARNING`、`XERROR`、`ERROR_code` 和小写 `warn/error` 不匹配。其他语言保留原有语法。

验证：原生 AppKit 的屏幕 RGB、默认主题、切换主题和日志关键词边界检查通过；
终端 UI 回归另存于 `build/teasp-ui.log`，原生测试结果见 `build/teasp-test.log`。

原生窗口行高去掉额外的 3 像素留白。文本/日志正文的 `*` 用橙色粗体高亮。

未保存状态用 `*` 标记，使用光标的绿色背景单独高亮；撤销到未修改状态后自动清除。

Normal 的 `/`、`?`、`n/N` 对可见匹配项使用选区的浅白底色；`:noh` 可清除。
