# Visual C++ 6.0 / NMAKE build. Run from windows\vc6 (or use build.bat).
!IFNDEF CFG
CFG=Release
!ENDIF
!IF "$(CFG)" != "Release" && "$(CFG)" != "Debug"
!ERROR CFG must be Release or Debug.
!ENDIF

CC=cl.exe
LINK=link.exe
AR=lib.exe
RC=rc.exe
OUTDIR=..\bin\vc6\$(CFG)
OBJDIR=$(OUTDIR)\obj
LIBS="$(OUTDIR)\joe.lib" "$(OUTDIR)\putty.lib" "$(OUTDIR)\common.lib"
SYSLIBS=kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib comctl32.lib imm32.lib winmm.lib

CFLAGS=/nologo /W3 /TC /DWIN32 /DJOEWIN_VC6 /I"." /I"..\src\common"
JOEFLAGS=/DJOEWIN /I"..\src\joe\win" /I"..\..\joe"
PUTTYFLAGS=/DJOEWIN /DSECURITY_WIN32 /D_WINDOWS /I"..\src\putty\putty" /I"..\src\putty\putty\windows" /I"..\src\putty\putty\charset" /I"..\src\putty\win"
COMMONFLAGS=/DUNICODE /D_UNICODE
RCFLAGS=/dJOEWIN_VC6 /i"." /i"..\src\common" /i"..\src\exe"
LFLAGS=/nologo /machine:I386 /subsystem:windows,5.01 /incremental:no
!IF "$(CFG)" == "Debug"
CFLAGS=$(CFLAGS) /MTd /Od /Zi /D_DEBUG /DDEBUG /Fd"$(OBJDIR)\vc6.pdb"
RCFLAGS=$(RCFLAGS) /d_DEBUG /dDEBUG
LFLAGS=$(LFLAGS) /debug
!ELSE
CFLAGS=$(CFLAGS) /MT /O2 /DNDEBUG
RCFLAGS=$(RCFLAGS) /dNDEBUG
!ENDIF

HEADERS=jwversion.h ..\src\common\*.h ..\src\joe\win\*.h ..\src\putty\win\*.h ..\src\putty\putty\*.h ..\src\putty\putty\windows\*.h ..\..\joe\*.h "$(OBJDIR)\dirs.tag"

all: "$(OUTDIR)\joe.exe"

"$(OBJDIR)\dirs.tag":
	@if not exist "$(OBJDIR)" mkdir "$(OBJDIR)"
	@echo VC6 build directory>"$@"

!INCLUDE sources.mak

"$(OUTDIR)\joe.lib": $(JOE_OBJS)
	$(AR) /nologo /out:"$@" @<<
$(JOE_OBJS)
<<

"$(OUTDIR)\putty.lib": $(PUTTY_OBJS)
	$(AR) /nologo /out:"$@" @<<
$(PUTTY_OBJS)
<<

"$(OUTDIR)\common.lib": $(COMMON_OBJS)
	$(AR) /nologo /out:"$@" @<<
$(COMMON_OBJS)
<<

"$(OBJDIR)\main.obj": ..\src\exe\standalone\main.c $(HEADERS)
	$(CC) $(CFLAGS) $(COMMONFLAGS) /Fo"$@" /c ..\src\exe\standalone\main.c

RESOURCEDEPS=dialogs.rc builtins.rc joewin-vc6.ico ..\src\exe\joewin.rc $(HEADERS) ..\..\syntax\*.jsf ..\..\colors\*.jcf ..\colors\*.jcf resources\*.dfl ..\rc\ftyperc ..\docs\license.txt

# VC6 LINK accepts one .res per output; include every resource in that one file.
"$(OBJDIR)\joe.res": joe.rc ..\rc\joerc $(RESOURCEDEPS)
	$(RC) $(RCFLAGS) /fo"$@" joe.rc
EXEDEPS=$(LIBS) "$(OBJDIR)\main.obj"
"$(OUTDIR)\joe.exe": $(EXEDEPS) "$(OBJDIR)\joe.res"
	$(LINK) $(LFLAGS) /out:"$@" $(EXEDEPS) "$(OBJDIR)\joe.res" $(SYSLIBS)
# Optional shared-DLL layout, matching the original Visual Studio solution.
dll: "$(OUTDIR)\dll\joewin.dll" "$(OUTDIR)\dll\joe.exe"

"$(OBJDIR)\joedll.obj": ..\src\exe\joedll\joedll.c $(HEADERS) ..\src\exe\joedll\joedll.h
	$(CC) $(CFLAGS) $(COMMONFLAGS) /DJOEDLL_EXPORTS /Fo"$@" /c ..\src\exe\joedll\joedll.c
"$(OBJDIR)\dllmain.obj": ..\src\exe\main.c $(HEADERS) ..\src\exe\joedll\joedll.h
	$(CC) $(CFLAGS) $(COMMONFLAGS) /I"..\src\exe\joedll" /Fo"$@" /c ..\src\exe\main.c
"$(OBJDIR)\joedll.res": joedll.rc $(RESOURCEDEPS)
	$(RC) $(RCFLAGS) /fo"$@" joedll.rc
"$(OUTDIR)\dll\joewin.dll": $(LIBS) "$(OBJDIR)\joedll.obj" "$(OBJDIR)\joedll.res"
	@if not exist "$(OUTDIR)\dll" mkdir "$(OUTDIR)\dll"
	$(LINK) $(LFLAGS) /dll /out:"$@" /implib:"$(OUTDIR)\dll\joewin.lib" $(LIBS) "$(OBJDIR)\joedll.obj" "$(OBJDIR)\joedll.res" $(SYSLIBS)

"$(OBJDIR)\joe_dll.res": joe.rc ..\rc\joerc $(RESOURCEDEPS)
	$(RC) $(RCFLAGS) /dJOEWIN_DLL_FRONTEND /fo"$@" joe.rc
DLLDEPS="$(OUTDIR)\dll\joewin.dll" "$(OBJDIR)\dllmain.obj"
DLLLINK="$(OUTDIR)\dll\joewin.lib" "$(OBJDIR)\dllmain.obj" $(SYSLIBS)
"$(OUTDIR)\dll\joe.exe": $(DLLDEPS) "$(OBJDIR)\joe_dll.res"
	$(LINK) $(LFLAGS) /out:"$@" $(DLLLINK) "$(OBJDIR)\joe_dll.res"
clean:
	@if exist "$(OUTDIR)" rmdir /s /q "$(OUTDIR)"

package: all
	@if not exist "$(OUTDIR)\colors" mkdir "$(OUTDIR)\colors"
	@copy /y ..\..\colors\*.jcf "$(OUTDIR)\colors\" >nul
	@copy /y ..\colors\*.jcf "$(OUTDIR)\colors\" >nul
	@if not exist "$(OUTDIR)\docs" mkdir "$(OUTDIR)\docs"
	@copy /y docs\*.html "$(OUTDIR)\docs\" >nul
