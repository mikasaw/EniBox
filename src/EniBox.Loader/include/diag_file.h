#ifndef ENIBOX_DIAG_FILE_H
#define ENIBOX_DIAG_FILE_H
#include <windows.h>
#include <stdio.h>

/* 注入/继承链路的文件诊断：追加写 %TEMP%\EniBox_diag_<pid>.log。
 * 每行带线程 ID；写失败静默（诊断不能成为故障源）。
 * 生产环境同样可用——接 DebugView 或读取 %TEMP% 文件即可取证，
 * 用于 §5.1.5（RUNBOOK）记录的 CI runner W 族继承失效排查。 */
static void EniBox_DiagLine(const char* line) {
    char path[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, path);
    if (n == 0 || n > MAX_PATH - 48) return;
    lstrcatA(path, "EniBox_diag_");
    char pid[16];
    sprintf_s(pid, sizeof(pid), "%lu", (unsigned long)GetCurrentProcessId());
    lstrcatA(path, pid);
    lstrcatA(path, ".log");

    HANDLE h = CreateFileA(path, FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    char stamp[40];
    sprintf_s(stamp, sizeof(stamp), "[tid %lu] ", (unsigned long)GetCurrentThreadId());
    WriteFile(h, stamp, (DWORD)lstrlenA(stamp), &written, NULL);
    WriteFile(h, line, (DWORD)lstrlenA(line), &written, NULL);
    WriteFile(h, "\r\n", 2, &written, NULL);
    CloseHandle(h);
}

#endif
