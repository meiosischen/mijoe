from pathlib import Path
import os, subprocess, sys

root = Path(__file__).resolve().parents[4] / 'windows/bin/vc6'
work = Path(__file__).resolve().parent.parent
env = dict(os.environ, WINEPREFIX='/tmp/joewin-vc6-tools/prefix', WINEDEBUG='-all', MVK_CONFIG_LOG_LEVEL='0')
wine = '/tmp/joewin-vc6-tools/Wine Stable.app/Contents/Resources/wine/bin/wine'
mode = sys.argv[1]
cases = [(cfg, sub, 'joe') for cfg in ['Release', 'Debug'] for sub in ['', 'dll']]
def check(case):
    cfg, sub, name = case
    path = root / cfg / sub / (name + '.exe')
    cmd = [wine, r'Z:\tmp\joewin-4.6-vc6-build\smoke.exe', 'Z:' + str(path).replace('/', '\\')]
    p = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=45)
    text = p.stdout.decode(errors='replace').replace('\r\r\n', '\n').replace('\r\n', '\n')
    label = '-'.join(['default-joe', mode, cfg.lower(), sub or 'standalone', name])
    (work / (label + '.log')).write_text(text)
    print(label, 'exit', p.returncode, text.strip().splitlines()[-1:] or [], flush=True)
    return bool(p.returncode)

results = [check(case) for case in cases]
sys.exit(any(results))
