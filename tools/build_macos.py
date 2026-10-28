#!/usr/bin/env python3
"""Build a relocatable native macOS terminal package, without a system install."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 2, 8))
args = parser.parse_args()
if platform.system() != "Darwin":
    parser.error("Run this builder on macOS with the Xcode command-line tools.")
if args.jobs < 1:
    parser.error("--jobs must be positive")
root = Path(__file__).resolve().parents[1]
arch = platform.machine()
build = root / "build/macos" / arch
objects = build / "objects"
package = root / "build/macos" / ("MiJoe-macOS-" + arch)
objects.mkdir(parents=True, exist_ok=True)
package.mkdir(parents=True, exist_ok=True)
env = dict(os.environ)
env["CPPFLAGS"] = (env.get("CPPFLAGS", "") + " -DMIJOE_PORTABLE_MACOS").strip()


def run(command, directory, log):
    print("Running:", " ".join(map(str, command)), flush=True)
    with (build / log).open("wb") as output:
        result = subprocess.run(command, cwd=directory, env=env, stdout=output, stderr=subprocess.STDOUT)
    if result.returncode:
        print((build / log).read_text(errors="replace")[-8000:])
        raise SystemExit(result.returncode)


def copy_asset(source, destination):
    # Atomic replacement leaves an already-running editor's executable inode
    # intact, so rebuilding never truncates or kills that editing session.
    destination = Path(destination)
    descriptor, temporary = tempfile.mkstemp(prefix="." + destination.name + "-", dir=destination.parent)
    os.close(descriptor)
    try:
        shutil.copy2(source, temporary)
        os.replace(temporary, destination)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    return str(destination)


run(["sh", "autojoe"], root, "bootstrap.log")
run([str(root / "configure"), "--prefix=/usr/local"], objects, "configure.log")
run(["make", f"-j{args.jobs}"], objects, "build.log")
with tempfile.TemporaryDirectory(prefix="install-", dir=build) as temporary:
    run(["make", "install", "DESTDIR=" + temporary], objects, "install.log")
    shutil.copytree(Path(temporary) / "usr/local", package, dirs_exist_ok=True, copy_function=copy_asset)

binary = package / "bin/joe"
dependencies = subprocess.check_output(["otool", "-L", str(binary)], text=True)
for line in dependencies.splitlines()[1:]:
    dependency = line.strip().split(" ")[0]
    if not dependency.startswith(("/usr/lib/", "/System/Library/")):
        raise SystemExit("Unexpected non-system runtime dependency: " + dependency)

cli = '''#!/bin/sh
set -eu
mijoe_bin_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "$mijoe_bin_dir/joe" "$@"
'''
(package / "bin/mijoe").write_text(cli)
(package / "bin/mijoe").chmod(0o755)
launcher = '''#!/bin/sh
set -eu
mijoe_package_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$mijoe_package_dir"
if [ "$#" -eq 0 ]; then
    set -- "$mijoe_package_dir/试用练习.txt"
fi
exec "$mijoe_package_dir/bin/mijoe" -nonotice --joe_state "$@"
'''
(package / "启动MiJoe.command").write_text(launcher)
(package / "启动MiJoe.command").chmod(0o755)
practice = package / "试用练习.txt"
if not practice.exists():
    practice.write_text("""MiJoe macOS 本机试用

当前默认是 NORMAL 模式。
按 i 进入插入模式，可以输入中文；按 Esc 返回。
用 h j k l 移动；v 按字符选择，V 按行选择。
dd 删除一行，yy 复制一行，p 粘贴；u 撤销，Ctrl+r 重做，. 重复修改。
输入 :w 后回车保存；:saveas 新文件名 另存为；:q 退出。
打开其他文件：:e 文件路径；:bn / :bp 切换缓冲区。
macOS 终端中使用 Command+V 粘贴。
更多说明见同目录的 操作说明.md。
""")
shutil.copy2(root / "docs/vim.md", package / "操作说明.md")
(package / "README.txt").write_text("""MiJoe macOS 本机测试版

双击“启动MiJoe.command”在系统终端中打开试用练习。
也可在任意终端运行：./bin/mijoe /完整路径/文件.txt
退出：Esc，然后 :q 回车；保存退出：:wq 回车。
传统 JOE 键位：./bin/mijoe -novim 文件.txt

程序为本机架构的原生终端应用。仅依赖 macOS 系统库。
bin、etc、share 需保留在同一目录，可一起移动。
不会安装到系统目录，也不修改 shell 配置或用户 HOME。
双击入口关闭 joe_state 写入；命令行入口沿用 JOE 自身选项。
重复构建会更新程序与资源，但保留已有“试用练习.txt”。
""")
manifest = {
    "architecture": arch,
    "macos_build_host": platform.mac_ver()[0],
    "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
    "dependencies": dependencies,
    "sources": {name: hashlib.sha256((root / name).read_bytes()).hexdigest()
                for name in ("joe/vim.c", "joe/vim.h", "joe/vimhelp.h", "docs/vim-help.txt",
                             "joe/help.c", "joe/help.h", "joe/colors.c", "joe/colors.h", "joe/options.c",
                             "joe/menu.c", "joe/menu.h", "joe/mmenu.c",
                             "joe/macos.c", "joe/macos.h", "joe/types.h", "joe/main.c", "joe/ushell.c", "joe/termcap.c")},
}
(package / "build-info.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
deliveries = root / "build/deliveries"
deliveries.mkdir(exist_ok=True)
archive_path = deliveries / ("mijoe-vim-macos-" + arch + ".zip")
with zipfile.ZipFile(archive_path, "w", zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(package.rglob("*")):
        if path.is_file():
            archive.write(path, package.name + "/" + str(path.relative_to(package)))
with zipfile.ZipFile(archive_path) as archive:
    assert archive.testzip() is None
print(json.dumps({"package": str(package), "launcher": str(package / "启动MiJoe.command"),
                  "cli": str(package / "bin/mijoe"), "zip": str(archive_path)}, ensure_ascii=False, indent=2))
