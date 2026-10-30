#!/usr/bin/env python3
"""Build the VC6/XP typography probe and run it against owned Win32 editors."""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
for name in ('wine', 'prefix', 'vc6', 'output'):
    parser.add_argument('--' + name, type=Path, required=True)
parser.add_argument('executables', nargs='+', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
work = args.output.resolve()
work.mkdir(parents=True, exist_ok=True)
win = lambda p: 'Z:' + str(p.resolve()).replace('/', '\\')
vc = args.vc6 / 'VC98'
if not vc.is_dir():
    vc = args.vc6
env = dict(os.environ, WINEPREFIX=str(args.prefix.resolve()), WINEDEBUG='-all',
           WINEDLLOVERRIDES='mscoree,mshtml=d', MVK_CONFIG_LOG_LEVEL='0')
helper = work / 'typography-test.exe'
lines = ['@echo off',
         f'set "PATH={win(vc / "bin")};{win(vc.parent / "Common/MSDev98/bin")};%PATH%"',
         f'set "INCLUDE={win(vc / "include")}"', f'set "LIB={win(vc / "lib")}"',
         f'cl.exe /nologo /W3 /TC /MT /O2 /Fe"{win(helper)}" /Fo"{win(work / "probe.obj")}" '
         f'"{win(root / "tests/win32_typography_test.c")}" /link user32.lib advapi32.lib']
batch = work / 'compile.cmd'
batch.write_bytes(('\r\n'.join(lines) + '\r\n').encode('ascii'))
with (work / 'compile.log').open('wb') as log:
    result = subprocess.run([str(args.wine), 'cmd.exe', '/c', win(batch)], env=env,
                            cwd=work, stdout=log, stderr=subprocess.STDOUT, timeout=60)
if result.returncode:
    print((work / 'compile.log').read_text(errors='replace'))
    raise SystemExit(result.returncode)
for index, executable in enumerate(args.executables):
    case = work / f'case-{index}'
    case.mkdir(exist_ok=True)
    result = subprocess.run([str(args.wine), win(helper), win(executable), win(case)],
                            env=env, cwd=case, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=90)
    (case / 'run.log').write_bytes(result.stdout)
    print(executable, 'exit', result.returncode, flush=True)
    print(result.stdout.decode(errors='replace'), flush=True)
    if result.returncode:
        raise SystemExit(result.returncode)
