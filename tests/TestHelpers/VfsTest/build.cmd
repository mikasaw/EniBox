@echo off
REM ============================================================
REM VfsTest 构建脚本
REM 支持多版本 Visual Studio:
REM   1. 环境变量 %VCINSTALLDIR% (推荐, IDE 自动设置)
REM   2. vswhere.exe (VS2017+ 标准定位方式)
REM   3. 常见硬编码路径 (VS2026 + VS2022 Insiders)
REM ============================================================
setlocal

set "VCVARS="

REM 1. 优先使用 IDE 设置的环境变量
if defined VCVARSALL (
    if exist "%VCVARSALL%" set "VCVARS=%VCVARSALL%"
    goto :run_vcvars
)

REM 2. 用 vswhere 查找 (VS2017+ 标准路径)
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" (
            set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
            goto :run_vcvars
        )
    )
)

REM 3. 常见硬编码路径 fallback (按优先级尝试)
set "CANDIDATES=C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat G:\vs2026\VC\Auxiliary\Build\vcvars64.bat F:\vs2026\VC\Auxiliary\Build\vcvars64.bat C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
for %%p in (%CANDIDATES%) do (
    if exist "%%p" (
        set "VCVARS=%%p"
        goto :run_vcvars
    )
)

echo ERROR: Could not locate vcvars64.bat via environment, vswhere, or known paths
echo Tried:
echo   %VCVARSALL%
echo   %VSWHERE%
echo   %CANDIDATES%
exit /b 1

:run_vcvars
echo Using vcvars64.bat: %VCVARS%
call "%VCVARS%" >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: vcvars64.bat failed at "%VCVARS%"
    exit /b 1
)

cl.exe vfstest.c /Fe:VfsTest.exe advapi32.lib /nologo
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

if not exist publish mkdir publish
copy /y VfsTest.exe publish\ >nul
echo Build success: VfsTest.exe
endlocal