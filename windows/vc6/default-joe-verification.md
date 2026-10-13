# 默认 JOE 键位收敛验证

日期：2026-10-13。工程：`/Users/meiosis/Workspace/Projects/mijoe`。

只保留默认 JOE 键位和 `joe` 前端。已删除 JMACS、JSTAR（WordStar 仿真）、
JPICO、JICE、CJOE、RJOE 的配置、项目、资源和已有产品文件；同时清理 UNIX
安装别名、Windows solution、WiX 引用、同步脚本和当前使用文档。
旧发布说明和历史验证日志继续作为历史证据保存。

默认键位文件 `rc/joerc.in` 与 `windows/rc/joerc` 均与修改前逐字节一致。
启动统一读取用户或系统的 `joerc`，再回退到内置 `*joerc`，不根据程序名称
或界面语言选择其他键位文件。Windows 设置名称固定为 `joe`。
用户自定义 `joerc`、提示框/菜单/搜索的上下文键位，以及语法、主题和消息翻译资源保留。

## 验证结果

| 项目 | 结果与证据 |
| --- | --- |
| 真正 VC6/NMAKE Release 独立版增量构建、打包 | 成功，退出码 0：[日志](verification/default-joe-release-package.log) |
| 真正 VC6/NMAKE Release 共享版增量构建 | 成功，退出码 0：[日志](verification/default-joe-release-dll.log) |
| 真正 VC6/NMAKE Debug 独立版增量构建、打包 | 成功，退出码 0：[日志](verification/default-joe-debug-package.log) |
| 真正 VC6/NMAKE Debug 共享版增量构建 | 成功，退出码 0：[日志](verification/default-joe-debug-dll.log) |
| 四种 JOE 构建的 Wine 运行 | 启动、中文文件名、ASCII 编辑、保存、退出通过；18 个主题和 F1～F4 拦截通过：[Release 独立](verification/default-joe-full-release-standalone-joe.log)、[Release 共享](verification/default-joe-full-release-dll-joe.log)、[Debug 独立](verification/default-joe-full-debug-standalone-joe.log)、[Debug 共享](verification/default-joe-full-debug-dll-joe.log) |
| Release 独立版内置资源 | 通过实际 JOE/miniz 加载器解出 103 个资源，与当前源文件逐字节一致；只含默认 `joerc`，保留 `zh_TW.po`：[日志](verification/default-joe-resources.log) |
| 程序重命名 | 将本轮 Release 独立版复制为临时 `renamed.exe`，在 `conf/renamedrc` 提供修改过输入键位的有效旧名配置，仍按默认键位编辑、保存成功：[日志](verification/default-joe-renamed.log) |
| 静态构建引用 | 17 个项目/安装 XML 文件解析通过，项目引用与 solution GUID 引用有效；活动配置和构建文件无扩展键位引用 |
| 资源生成器 | 重复执行后内容及时间戳不变，不再生成其他键位资源 |
| 默认键位与 C 内置字符串表 | 两份默认键位 SHA-256 不变；修改后的内置字符串表单独 C 语法检查通过 |

本轮复用已有测试辅助 EXE/DLL，对当前工程重新生成的产品执行运行和解压检查。
构建是增量验证，未重新统计原工程全量重建中的已有警告。

提交前已清理 VC6 的 `obj` 目录、静态/导入库、链接中间文件和调试符号，
并加入 `.gitignore`；可运行 EXE/DLL、主题、帮助和上述验证日志保留。
本轮专用的 `/tmp/mijoe-default-joe-verification` 临时目录也已删除。

## 产品

| 配置 | 程序 | SHA-256 |
| --- | --- | --- |
| Release 独立版 | [joe.exe](../bin/vc6/Release/joe.exe) | `cc83879ed8426465c74aa8d7ed2c7599c4c5033950e0784ba0f5f6ccd0d36351` |
| Release 共享版 | [joe.exe](../bin/vc6/Release/dll/joe.exe)，同目录 `joewin.dll` | `c17910e6226c81584dbb36f500f0bed0054bff8b459942af4b228bf109848b69` |
| Debug 独立版 | [joe.exe](../bin/vc6/Debug/joe.exe) | `6a664eb54872147c854d391bf53d766874b29bf8aa195399d69f27983e45fc6a` |
| Debug 共享版 | [joe.exe](../bin/vc6/Debug/dll/joe.exe)，同目录 `joewin.dll` | `18d3ec6a3db6c2a781460e536f0761aeaf00d3c574efb09d8deb5c46bd97bf59` |

默认键位文件 SHA-256：

- `rc/joerc.in`：`34ea55ab4a52fd9ca64c73f0f80fdf1f874b56110000d919973bb6adffcc7fa0`
- `windows/rc/joerc`：`6e753c7ba1ab677dd51f17f6b53c26492f63c9d1b24e9c8969f29faf4eec3567`

## 验证边界

Windows XP 实机/UTM、中文输入法、现代 Visual Studio 构建和 WiX 安装包未运行验证。
UNIX 完整构建未执行；本源码树未生成 UNIX 所需的 `autoconf.h`，直接编译其内置表
会被该环境前置条件阻止，因此只对表内容做独立语法检查。
Vim 功能仍处于源码分析阶段，本次实现范围为默认 JOE 键位收敛。
