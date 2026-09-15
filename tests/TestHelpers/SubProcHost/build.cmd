@echo off
REM 构建 native SubProcHost（.NET 版被 Defender 隔离，见 host.c 头注释）
REM 用法: 在装了 MSVC 的环境运行，或直接 cmd /c build.cmd
setlocal
where cl.exe >nul 2>&1 || (echo ERROR: run from a VS dev prompt & exit /b 1)
cl.exe /nologo /W3 /O2 host.c /Fe:SubProcHost.exe kernel32.lib
if errorlevel 1 exit /b 1
if not exist publish mkdir publish
copy /y SubProcHost.exe publish\ >nul
echo Build success: publish\SubProcHost.exe
endlocal
