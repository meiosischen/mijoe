from pathlib import Path
import subprocess, os, sys

root = Path('/Users/meiosis/Downloads/261011/joewin-4.6/windows/vc6')
work = Path(__file__).parent
wine = '/tmp/joewin-vc6-tools/Wine Stable.app/Contents/Resources/wine/bin/wine'
base = r'Z:\Users\meiosis\Documents\UTM\XP\vc6'
env = dict(os.environ, WINEPREFIX='/tmp/joewin-vc6-tools/prefix', WINEDEBUG='-all',
           MVK_CONFIG_LOG_LEVEL='0', WINEDLLOVERRIDES='mscoree,mshtml=d',
           WINEPATH=base+r'\VC98\BIN;'+base+r'\COMMON\MSDEV98\BIN;'+base+r'\OS\SYSTEM',
           INCLUDE=base+r'\VC98\INCLUDE', LIB=base+r'\VC98\LIB')
config = sys.argv[1]
log = work / ('ide-' + config.lower() + '.log')
cmd = [wine, base+r'\COMMON\MSDEV98\BIN\MSDEV.COM',
       'Z:' + str(root/'joewin.dsw').replace('/', '\\'), '/MAKE',
       'joewin - Win32 ' + config, '/REBUILD', '/OUT', 'Z:'+str(log).replace('/', '\\')]
with (work/('ide-'+config.lower()+'-console.log')).open('wb') as f:
    p = subprocess.run(cmd, cwd=root, env=env, stdout=f, stderr=subprocess.STDOUT, timeout=300)
s = log.read_text(errors='replace') if log.exists() else 'No build log created'
print('MSDEV', config, 'exit', p.returncode)
print('\n'.join(s.splitlines()[:2] + s.splitlines()[-3:]))
sys.exit(p.returncode)
