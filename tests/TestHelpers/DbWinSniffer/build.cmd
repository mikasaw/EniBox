@echo off
REM DBWIN OutputDebugString 全系统监听器（CI 诊断用，见 sniffer.c）
setlocal
where cl.exe >nul 2>&1 || (echo ERROR: run from a VS dev prompt & exit /b 1)
cl.exe /nologo /W3 /O2 sniffer.c /Fe:dbwin_sniffer.exe kernel32.lib user32.lib
if errorlevel 1 exit /b 1
if not exist publish mkdir publish
copy /y dbwin_sniffer.exe publish\ >nul
echo Build success: publish\dbwin_sniffer.exe
endlocal
