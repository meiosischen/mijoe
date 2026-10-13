@echo off 

kdiff3 %~dp0\..\..\..\rc\joerc.in %~dp0\..\..\rc\joerc -o %~dp0\..\..\rc\joerc
kdiff3 %~dp0\..\..\..\rc\ftyperc %~dp0\..\..\rc\ftyperc -o %~dp0\..\..\rc\ftyperc
