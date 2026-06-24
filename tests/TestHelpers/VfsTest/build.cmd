@echo off
call "F:\vs2026\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Visual Studio 2022 not found at standard location
    exit /b 1
)
cl.exe vfstest.c /Fe:VfsTest.exe advapi32.lib /nologo
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%
if not exist publish mkdir publish
copy /y VfsTest.exe publish\ >nul
echo Build success: VfsTest.exe
