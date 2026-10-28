#!/usr/bin/env python3
"""Run the unchanged Windows build entry point with real VC6 under Wine."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--wine", required=True, type=Path)
parser.add_argument("--prefix", required=True, type=Path)
parser.add_argument("--vc6", required=True, type=Path)
parser.add_argument("--config", choices=("Release", "Debug"), default="Release")
parser.add_argument("--target", choices=("all", "package", "dll"), default="package")
parser.add_argument("--log", required=True, type=Path)
parser.add_argument("--smoke", action="store_true", help="Compile and run child-window input tests")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]


def winpath(path):
    return "Z:" + str(path.resolve()).replace("/", "\\")


env = dict(os.environ, WINEPREFIX=str(args.prefix.resolve()), WINEDEBUG="-all",
           WINEDLLOVERRIDES="mscoree,mshtml=d", MVK_CONFIG_LOG_LEVEL="0",
           VC6_ROOT=winpath(args.vc6))
args.log.parent.mkdir(parents=True, exist_ok=True)
with args.log.open("wb") as log:
    result = subprocess.run([str(args.wine.resolve()), "cmd.exe", "/c", "build.bat",
                             args.config, args.target], cwd=root / "windows/vc6",
                            env=env, stdout=log, stderr=subprocess.STDOUT, timeout=600)
text = args.log.read_text(errors="replace")
errors = [line for line in text.splitlines() if "error " in line.lower() or "fatal error" in line.lower()]
print(f"VC6 {args.config} {args.target}: exit={result.returncode}; log={args.log}")
print("\n".join(errors[:30] if errors else text.splitlines()[-5:]), flush=True)
if result.returncode or not args.smoke:
    sys.exit(result.returncode)

work = Path(tempfile.mkdtemp(prefix="mijoe-win32-smoke-" + args.config.lower() + "-" + args.target + "-", dir=args.log.parent))
print(f"Windows UI test files: {work}", flush=True)
vc98 = args.vc6 / "VC98" if (args.vc6 / "VC98").is_dir() else args.vc6
helper = work / "vim-smoke.exe"
batch = work / "compile.cmd"
batch.write_bytes((
    '@echo off\r\n'
    f'set "PATH={winpath(vc98 / "bin")};{winpath(vc98.parent / "Common/MSDev98/bin")};%PATH%"\r\n'
    f'set "INCLUDE={winpath(vc98 / "include")}"\r\n'
    f'set "LIB={winpath(vc98 / "lib")}"\r\n'
    f'cl.exe /nologo /W3 /TC /MT /O2 /Fe"{winpath(helper)}" '
    f'/Fo"{winpath(work / "vim-smoke.obj")}" "{winpath(root / "tests/vim_win32_smoke.c")}" /link user32.lib\r\n'
).encode("ascii"))
subprocess.run([str(args.wine), "cmd.exe", "/c", winpath(batch)], env=env, cwd=work, check=True, timeout=60)
executable = root / "windows/bin/vc6" / args.config
if args.target == "dll":
    executable /= "dll"
executable /= "joe.exe"
cases = [
    ("insert", b"initial\n", b"iX\x1b:wq\r", b"Xinitial\n"),
    ("delete-undo-dot", b"a\nb\nc\nd\n", b"dd.u\x12:wq\r", b"c\nd\n"),
    ("visual", b"abcdef\n", b"vlld:wq\r", b"def\n"),
    ("visual-line-change", b"a\nb\nc\n", b"VjcX\x1b:wq\r", b"X\nc\n"),
    ("copy-paste", b"one two\n", b"ywwp:wq\r", b"one tone wo\n"),
    ("save-failure", b"a\n", b"A!\x1b:wq missing/file\r:wq\r", b"a!\n"),
    ("buffers", b"a\n", b"A!\x1b:enew\riother\x1b:w other.txt\r:b 1\r:w\r:q!\r", b"a!\n"),
    ("saveas", b"a\n", b"A!\x1b:saveas renamed.txt\rA?\x1b:w\r:q!\r", b"a\n"),
    ("windows-path", b"a\n", b":w folder\\copy.txt\r:q!\r", b"a\n"),
    ("command-paste", b"a\n", b":w \x1b[200~folder\\pasted.txt\x1b[201~\r:q!\r", b"a\n"),
    ("find-forward-delete", b"abxcxd tail\n", b"dfx:wq\r", b"cxd tail\n"),
    ("find-backward-delete", b"abxcxd tail\n", b"$dFx:wq\r", b"abxcl\n"),
    ("till-forward-delete", b"abxcxd tail\n", b"dtx:wq\r", b"xcxd tail\n"),
    ("till-backward-delete", b"abxcxd tail\n", b"$dTx:wq\r", b"abxcxl\n"),
    ("till-count-repeat", b"aXbXcXdX tail\n", b"tX2;x:wq\r", b"aXXcXdX tail\n"),
    ("find-reverse-repeat", b"aXbXcXdX tail\n", b"2fX,;x:wq\r", b"aXbcXdX tail\n"),
    ("find-missing-count", b"abXcd\nX next\n", b"d2fX:wq\r", b"abXcd\nX next\n"),
    ("percent-pair", b"(a(b)c) tail\n", b"d%:wq\r", b" tail\n"),
    ("percent-quote", b"(a ')' b) tail\n", b"%x:wq\r", b"(a ')' b tail\n"),
    ("percent-comment", b"/* comment */ tail\n", b"%x:wq\r", b"/* comment * tail\n"),
    ("percent-directive", b"#if A\nA\n#else\nB\n#endif\n", b"d%:wq\r", b"B\n#endif\n"),
    ("percent-number", b"a\nb\nc\nd\ne\nf\ng\n", b"d50%:wq\r", b"e\nf\ng\n"),
    ("help-pages", b"abcdef\n", b":help\r\x1b[6~\x1b[5~qA!\x1b:wq\r", b"abcdef!\n"),
    ("help-undo", b"abcdef\n", b"x:h\rxdd:enew\r\x1bu:wq\r", b"abcdef\n"),
    ("native-help", b"abcdef\n", b"\x0bH\x1b[6~\x1b[5~\x0bh:wq\r", b"abcdef\n"),
    ("native-window-shortcut", b"abcdef\n", b"\x0bTx:wq\r", b"bcdef\n"),
    ("options-theme", b"abcdef\n", b"\x14Sgruvbox\r:wq\r", b"abcdef\n"),
    ("color-menu", b"abcdef\n", b":colors\rg:colorscheme\r\x03:wq\r", b"abcdef\n"),
    ("insert-options-menu", b"abcdef\n", b"iX\x14\x03Y\x1b:wq\r", b"XYabcdef\n"),
    ("options-toggle-insert", b"abcdef\n", b"\x14\x14iXY\x1b:wq\r", b"XYabcdef\n"),
    ("options-toggle-overtype", b"abcdef\n", b"\x14T\x14\x14iXY\x1b:wq\r", b"XYcdef\n"),
    ("insert-options-toggle", b"abcdef\n", b"iX\x14\x14Y\x1b:wq\r", b"XYabcdef\n"),
    ("picture-normal", b"abcdef\n", b"\x14P8liX\x1b:wq\r", b"abcdef  X\n"),
    ("picture-insert-arrows", b"abcdef\n", b"\x14Pi" + b"\x1b[C" * 8 + b"X\x1b:wq\r", b"abcdef  X\n"),
    ("picture-vertical", b"abcdef\nx\n", b"\x14P$jiX\x1b:wq\r", b"abcdef\nx    X\n"),
    ("picture-append", b"abcdef\n", b"\x14P8laX\x1b:wq\r", b"abcdef   X\n"),
    ("picture-down-undo", b"ab", b"\x14P2ju:wq\r", b"ab"),
    ("picture-readonly", b"ab", b"\x14P\x14O3j:w!\r:q!\r", b"ab"),
    ("picture-undo-column", b"abcdef\n", b"\x14P8liX\x1buiY\x1b:wq\r", b"abcdefY\n"),
    ("picture-line-change", b"abc\ndef\n", b"\x14PllccX\x1b:wq\r", b"X\ndef\n"),
    ("picture-block-insert", b"abc\ndef\nghi\n", b"\x14P\x16lljjIX\x1b:wq\r", b"Xabc\nXdef\nXghi\n"),
    ("help-paste", b"abcdef\n", b":help\r\x1b[200~\x0bHiBAD\x1b:wq\r\x1b[201~qx:wq\r", b"bcdef\n"),
    ("block-insert", b"abc\ndef\nghi\n", b"\x16jjI// \x1b:wq\r", b"// abc\n// def\n// ghi\n"),
    ("block-insert-short", b"abcde\nx\n\nABCDE\n", b"ll\x16jjjIX\x1b:wq\r", b"abXcde\nx\n\nABXCDE\n"),
    ("block-append-pad", b"abcde\nx\n\nABCDE\n", b"ll\x16jjjAX\x1b:wq\r", b"abcXde\nx  X\n   X\nABCXDE\n"),
    ("block-append-eol", b"ab\ncdef\nx\n", b"\x16jj$AX\x1b:wq\r", b"abX\ncdefX\nxX\n"),
    ("block-tab", b"abcd\n\tXYZ\nZZZZ\n", b"ll\x16jjIX\x1b:wq\r", b"abXcd\n  X      XYZ\nZZXZZ\n"),
    ("block-undo-redo", b"abc\ndef\nghi\n", b"\x16jjIXY\x1bu\x12:wq\r", b"XYabc\nXYdef\nXYghi\n"),
    ("block-paste", b"abc\ndef\n", b"\x16jI\x1b[200~X\x1b[201~\x1b:wq\r", b"Xabc\nXdef\n"),
    ("block-cancel", b"abc\ndef\n", b"\x16j\x1bx:wq\r", b"abc\nef\n"),
]
for name, initial, keys, expected in cases:
    directory = work / name
    directory.mkdir(exist_ok=True)
    (directory / "folder").mkdir(exist_ok=True)
    (directory / "initial.bin").write_bytes(initial)
    (directory / "keys.bin").write_bytes(keys)
    run = subprocess.run([str(args.wine), winpath(helper), winpath(executable), winpath(directory)],
                         env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=40)
    (directory / "run.log").write_bytes(run.stdout)
    actual = (directory / "result.bin").read_bytes() if (directory / "result.bin").exists() else None
    if run.returncode or actual != expected:
        print(f"FAIL {name}: exit={run.returncode}, actual={actual!r}, expected={expected!r}")
        print(run.stdout.decode(errors="replace"))
        sys.exit(1)
    if name == "saveas" and (directory / "renamed.txt").read_bytes() != b"a!?\n":
        raise AssertionError("saveas did not change the active filename")
    if name == "windows-path" and (directory / "folder/copy.txt").read_bytes() != b"a\n":
        raise AssertionError("Windows path separators were not preserved")
    if name == "command-paste" and (directory / "folder/pasted.txt").read_bytes() != b"a\n":
        raise AssertionError("Pasting a command-line filename failed")
    print(f"PASS {name}", flush=True)
