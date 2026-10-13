@echo off
set "PATH=Z:\Users\meiosis\Documents\UTM\XP\vc6\VC98\BIN;Z:\Users\meiosis\Documents\UTM\XP\vc6\COMMON\MSDEV98\BIN;%PATH%"
set "INCLUDE=Z:\Users\meiosis\Documents\UTM\XP\vc6\VC98\INCLUDE"
set "LIB=Z:\Users\meiosis\Documents\UTM\XP\vc6\VC98\LIB"
cl.exe /nologo /W3 /MT /FeZ:\tmp\joewin-4.6-vc6-build\smoke.exe /FoZ:\tmp\joewin-4.6-vc6-build\smoke.obj Z:\tmp\joewin-4.6-vc6-build\smoke.c user32.lib kernel32.lib gdi32.lib
cl.exe /nologo /W3 /MT /LD /FeZ:\tmp\joewin-4.6-vc6-build\theme_probe.dll /FoZ:\tmp\joewin-4.6-vc6-build\theme_probe.obj Z:\tmp\joewin-4.6-vc6-build\theme_probe.c user32.lib kernel32.lib
