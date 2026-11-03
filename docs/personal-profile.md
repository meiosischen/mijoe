# 个人配置与最近文件

MiJoe 的个人全局配置统一使用 `~/.mj`。Windows 的 `~` 是当前用户的 `USERPROFILE`，例如 `C:\Documents and Settings\用户名\.mj`。
程序自带的默认配置和主题仍随安装包提供，个人修改放入下列目录即可。

| 路径 | 用途 |
| --- | --- |
| `config` | 简单的全局选项，每行一个 JOE 选项；优先于文件类型默认值，命令行仍可显式覆盖 |
| `joerc` | 完整的个人 JOE 按键/菜单配置；没有此文件时使用程序自带按键配置 |
| `state` | 编辑状态、搜索等历史、主题状态；遵循 `joe_state` 开关 |
| `.recent` | 最近打开的 200 个文件及模式，不受 `joe_state` 开关影响 |
| `colors/`、`syntax/`、`charmaps/` | 个人主题、语法和字符映射 |
| `macos.json` | macOS 窗口版字号、行距 |
| `windows.ini` | Windows 窗口大小/位置、字体、行距和光标设置 |

首次创建时会生成带注释的 `config`，默认没有启用额外选项。例如，只需显示层软折行可以配置：

```text
-softwrap
-rmargin 100
--wordwrap
-mouse
```

`-softwrap` 只改变显示；`--wordwrap` 关闭原有的输入时硬换行。配置文件在下次启动时读取。
可用环境变量 `MIJOE_CONFIG_HOME` 指定其他独立配置目录，适合便携使用和隔离测试；不设置时就是 `~/.mj`。

## 迁移

首次升级会把原来的 `~/.joerc`、`~/.joe_state` 和 `~/.joe` 内的个人资源复制到新目录，已有新配置不会被覆盖。
Windows 兼容原来的 JoeEditor 个人目录；旧注册表里的窗口设置作为读取回退，后续保存写入 `windows.ini`。
macOS 兼容读取原应用的字号/行距偏好，后续保存写入 `macos.json`。原文件和旧偏好保留作备份。
`.migrated` 标记首次迁移已经完成；修改已迁移的配置请使用新目录。

## `:recent`

列表最新在前，同一个路径重复打开时移到顶部，最多保留 200 条。长路径自动折行，不截掉文件名；
超过弹窗高度时显示滚动位置。上下键或 Ctrl+P/N 选择文件，PageUp/PageDown 按显示页翻页，
Enter 打开，Esc / Ctrl+C 关闭。Home / `g` 到第一项，End / `G` 到最后一项。

每条记录保存只读/读写、文本/Hex、只读懒加载及软折行标志。打开前检查文件，恢复记录中的模式，
并保留其他缓冲区的未保存内容。大文件的只读懒加载模式可直接恢复，不再重复询问。
已经不存在的文件会显示错误并移除记录；权限、读取失败等其他错误保留记录，以便修复后重试。

`.recent` 是 UTF-8 文本，首行标记版本，其余每行是数字标志、Tab 和带转义的绝对路径：

```text
# MiJoe recent v1: flags (RO=1 HEX=2 LAZY=4 SOFT=8), quoted UTF-8 absolute path
0	"/Users/example/notes.txt"
3	"/Users/example/readonly-binary.dat"
5	"/Users/example/large.log"
```

上例中的 `\t` 表示一个实际 Tab。数字是标志之和，`0` 为普通读写文本，`3` 为只读 Hex，`5` 为只读懒加载文本。
路径中的引号、反斜杠和换行进行转义，打开时作为文件名处理，不作为 Ex 命令执行。
历史即时保存，文件锁和原子替换保护多个窗口的更新；`.recent.lock` 为内部锁文件。
无法读取或版本格式不受支持时会提示，不会用空历史覆盖原文件。
