from pathlib import Path
import os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

root = Path('/Users/meiosis/Downloads/261011/joewin-4.6/windows/bin/vc6')
work = Path(__file__).parent
env = dict(os.environ, WINEPREFIX='/tmp/joewin-vc6-tools/prefix', WINEDEBUG='-all', MVK_CONFIG_LOG_LEVEL='0')
wine = '/tmp/joewin-vc6-tools/Wine Stable.app/Contents/Resources/wine/bin/wine'
mode = sys.argv[1]
cases = [(cfg, sub, 'joe') for cfg in ['Release', 'Debug'] for sub in ['', 'dll']]
if mode == 'profiles':
    cases = [(cfg, sub, name) for cfg in ['Release', 'Debug'] for sub in ['', 'dll']
             for name in ['jmacs', 'jstar', 'jpico', 'jice', 'cjoe']]
def check(case):
    cfg, sub, name = case
    path = root / cfg / sub / (name + '.exe')
    cmd = [wine, r'Z:\tmp\joewin-4.6-vc6-build\smoke.exe', 'Z:' + str(path).replace('/', '\\')]
    if mode == 'profiles': cmd.append('open-only')
    p = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=45)
    text = p.stdout.decode(errors='replace')
    label = '-'.join([mode, cfg.lower(), sub or 'standalone', name])
    (work / (label + '.log')).write_text(text)
    print(label, 'exit', p.returncode, text.strip().splitlines()[-1:] or [], flush=True)
    return bool(p.returncode)

if mode == "profiles":
    with ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(check, cases))
else:
    results = [check(case) for case in cases]
sys.exit(any(results))
