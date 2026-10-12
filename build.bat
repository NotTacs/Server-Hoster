@echo off
cd /d "%~dp0"
where cl >nul 2>nul || (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    call :findvs
)
cl /nologo /O2 /EHsc /std:c++17 /DUNICODE /D_UNICODE launcher.cpp /Fe:launcher.exe
del launcher.obj 2>nul
goto :eof

:findvs
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -property installationPath`) do set "VS=%%i"
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
goto :eof

pause