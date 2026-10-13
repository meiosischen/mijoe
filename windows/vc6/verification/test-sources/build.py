from pathlib import Path
import os, subprocess, sys

root = Path('/Users/meiosis/Downloads/261011/joewin-4.6')
work = Path(__file__).parent
wine = '/tmp/joewin-vc6-tools/Wine Stable.app/Contents/Resources/wine/bin/wine'
env = os.environ.copy()
env.update(WINEPREFIX='/tmp/joewin-vc6-tools/prefix', WINEDEBUG='-all',
           WINEDLLOVERRIDES='mscoree,mshtml=d', MVK_CONFIG_LOG_LEVEL='0',
           VC6_ROOT=r'Z:\Users\meiosis\Documents\UTM\XP\vc6')
config = sys.argv[1]
target = sys.argv[2] if len(sys.argv) > 2 else 'all'
log = work / (config.lower() + '-' + target + '.log')
with log.open('wb') as f:
    p = subprocess.run([wine, 'cmd.exe', '/c', 'build.bat', config, target],
                       cwd=root/'windows/vc6', env=env, stdout=f, stderr=subprocess.STDOUT, timeout=300)
s = log.read_text(errors='replace')
errors = [line for line in s.splitlines() if 'error ' in line.lower() or 'fatal error' in line.lower()]
warnings = [line for line in s.splitlines() if 'warning C' in line]
print(config, target, 'exit', p.returncode, 'warnings', len(warnings), 'log', log, flush=True)
print('\n'.join(errors[:40] if errors else s.splitlines()[-3:]), flush=True)
sys.exit(p.returncode)
