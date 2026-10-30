@echo off
cd /d "%~dp0"
ver > xp-system.txt
set failures=0
call :run "programs\Release\joe.exe" "cases\release-package\ctrl-c-clean"
call :run "programs\Release\joe.exe" "cases\release-package\ctrl-c-dirty-confirm"
call :run "programs\Release\joe.exe" "cases\release-package\ctrl-c-dirty-cancel"
call :run "programs\Release\joe.exe" "cases\release-package\ctrl-c-help"
call :run "programs\Release\joe.exe" "cases\release-package\ctrl-c-grep"
call :run "programs\Release\joe.exe" "cases\release-package\ctrl-c-buffers"
call :run "programs\Release\joe.exe" "cases\release-package\ctrl-c-shared-view"
call :run "programs\Release\dll\joe.exe" "cases\release-dll\ctrl-c-clean"
call :run "programs\Release\dll\joe.exe" "cases\release-dll\ctrl-c-dirty-confirm"
call :run "programs\Release\dll\joe.exe" "cases\release-dll\ctrl-c-dirty-cancel"
call :run "programs\Release\dll\joe.exe" "cases\release-dll\ctrl-c-help"
call :run "programs\Release\dll\joe.exe" "cases\release-dll\ctrl-c-grep"
call :run "programs\Release\dll\joe.exe" "cases\release-dll\ctrl-c-buffers"
call :run "programs\Release\dll\joe.exe" "cases\release-dll\ctrl-c-shared-view"
call :run "programs\Debug\joe.exe" "cases\debug-package\ctrl-c-clean"
call :run "programs\Debug\joe.exe" "cases\debug-package\ctrl-c-dirty-confirm"
call :run "programs\Debug\joe.exe" "cases\debug-package\ctrl-c-dirty-cancel"
call :run "programs\Debug\joe.exe" "cases\debug-package\ctrl-c-help"
call :run "programs\Debug\joe.exe" "cases\debug-package\ctrl-c-grep"
call :run "programs\Debug\joe.exe" "cases\debug-package\ctrl-c-buffers"
call :run "programs\Debug\joe.exe" "cases\debug-package\ctrl-c-shared-view"
call :run "programs\Debug\dll\joe.exe" "cases\debug-dll\ctrl-c-clean"
call :run "programs\Debug\dll\joe.exe" "cases\debug-dll\ctrl-c-dirty-confirm"
call :run "programs\Debug\dll\joe.exe" "cases\debug-dll\ctrl-c-dirty-cancel"
call :run "programs\Debug\dll\joe.exe" "cases\debug-dll\ctrl-c-help"
call :run "programs\Debug\dll\joe.exe" "cases\debug-dll\ctrl-c-grep"
call :run "programs\Debug\dll\joe.exe" "cases\debug-dll\ctrl-c-buffers"
call :run "programs\Debug\dll\joe.exe" "cases\debug-dll\ctrl-c-shared-view"
echo Failures: %failures% > summary.txt
type summary.txt
if "%~1"=="" pause
exit /b %failures%
:run
vim-smoke.exe "%CD%\%~1" "%CD%\%~2" --verify > "%~2\run.log" 2>&1
if errorlevel 1 goto failed
echo PASS %~1 %~2
exit /b 0
:failed
echo FAIL %~1 %~2
set /a failures+=1
exit /b 1
