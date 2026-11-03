# teasp 亮青符号验证

日期：2026-11-03。

C 语法把 `!=()` 等符号分配给 Control、Brace；只修改 Operator 不会覆盖这些符号。
本次将三个分类统一为亮 cyan：真彩色 `#00FFFF`、256 色 51、16 色 14。
关键字、注释、字符串及其余主题分类保留既有设置。

- 原生 AppKit：138 项检查通过，其中新增 8 项核对 C 的不等号、左右圆括号、左右花括号、逻辑与、比较及指针符号的实际屏幕 RGB。
- VC6：Release / Debug 的独立 EXE 与 DLL 版本全部构建成功；六个 PE 兼容性静态检查通过。
- Windows 资源：压缩资源解压后与源码一致，四个核心 EXE / DLL 均包含最新配色资源。
- 本机：已更新个人主题并保留备份，已安装新版 App；前后端、主题文件、签名和 `/usr/local/bin/mj` 软链核对通过。
- 已重新打开用户原先查看的 `joe/b.c`，恢复到第 269 行、第 1 列，目视确认括号和运算符为亮 cyan。

Windows 的构建与 PE 检查不能替代真实 XP 运行验证。
证据：[macOS](../macos/verification/teasp-cyan-20261103/)、
[VC6 与 PE](../windows/vc6/verification/teasp-cyan-20261103/)。
