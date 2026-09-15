/*
 * SubProcHost (native C rewrite, 2026-09-15)
 *
 * 为什么用 C：.NET 版的 obj/publish SubProcHost.dll 一编译出来就被
 * Defender 启发式隔离（进程启动 + 注入语义的组合特征），导致 SDK 的
 * CreateAppHost 找不到中间 dll 而构建失败（取证见 Get-MpThreatDetection）。
 * 原生 C 版本从未被隔离。
 *
 * 协议（供 E2E 断言）：
 *   每个子进程一组：
 *     CHECK:SUBPROC_CREATE:OK:pid=<pid> | CHECK:SUBPROC_CREATE:FAIL:...
 *     <子进程 stdout 原样透传>
 *     CHECK:SUBPROC_EXIT:OK:code=<code>
 *
 * 用法: SubProcHost <child_exe> [child_args...]
 * 本程序会用 CreateProcessA 与 CreateProcessW 各启动一次相同命令行的
 * 子进程 —— 覆盖 Hook_CreateProcessA/Hook_CreateProcessW 两条注入路径。
 */
#include <windows.h>
#include <stdio.h>

static BOOL RunChild(char* cmdLine, BOOL wide, const char* tag)
{
    char tmpPath[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, tmpPath);
    if (n == 0 || n >= MAX_PATH - 32) {
        printf("CHECK:SUBPROC_CREATE:FAIL:GetTempPath (%s)\n", tag);
        return FALSE;
    }
    lstrcatA(tmpPath, "subproc_host_out.txt");

    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hOut = CreateFileA(tmpPath, GENERIC_WRITE, FILE_SHARE_READ, &sa,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hOut == INVALID_HANDLE_VALUE) {
        printf("CHECK:SUBPROC_CREATE:FAIL:tmpfile GLE=%lu (%s)\n", GetLastError(), tag);
        return FALSE;
    }

    wchar_t wCmdLine[2048];
    if (wide) {
        size_t len = strlen(cmdLine);
        size_t outLen = 0;
        mbstowcs_s(&outLen, wCmdLine, 2048, cmdLine, _TRUNCATE);
    }

    STARTUPINFOA siA = {0};
    PROCESS_INFORMATION piA = {0};
    STARTUPINFOW siW = {0};
    PROCESS_INFORMATION piW = {0};
    BOOL ok;
    if (wide) {
        siW.cb = sizeof(siW);
        siW.dwFlags = STARTF_USESTDHANDLES;
        siW.hStdOutput = hOut;
        siW.hStdError = hOut;
        siW.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        ok = CreateProcessW(NULL, wCmdLine, NULL, NULL, TRUE, 0, NULL, NULL, &siW, &piW);
        piA = piW; /* 同构体布局，统一后续处理 */
    } else {
        siA.cb = sizeof(siA);
        siA.dwFlags = STARTF_USESTDHANDLES;
        siA.hStdOutput = hOut;
        siA.hStdError = hOut;
        siA.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        ok = CreateProcessA(NULL, cmdLine, NULL, NULL, TRUE, 0, NULL, NULL, &siA, &piA);
    }

    if (!ok) {
        printf("CHECK:SUBPROC_CREATE:FAIL:GLE=%lu (%s)\n", GetLastError(), tag);
        CloseHandle(hOut);
        DeleteFileA(tmpPath);
        return FALSE;
    }
    printf("CHECK:SUBPROC_CREATE:OK:pid=%lu (%s)\n", piA.dwProcessId, tag);
    fflush(stdout);

    WaitForSingleObject(piA.hProcess, 20000);
    DWORD code = 1;
    GetExitCodeProcess(piA.hProcess, &code);
    CloseHandle(piA.hThread);
    CloseHandle(piA.hProcess);
    CloseHandle(hOut);

    HANDLE hRead = CreateFileA(tmpPath, GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_EXISTING, 0, NULL);
    if (hRead != INVALID_HANDLE_VALUE) {
        char buf[4096];
        DWORD rd = 0;
        while (ReadFile(hRead, buf, sizeof(buf), &rd, NULL) && rd)
            fwrite(buf, 1, rd, stdout);
        CloseHandle(hRead);
    }
    DeleteFileA(tmpPath);

    printf("CHECK:SUBPROC_EXIT:OK:code=%lu (%s)\n", code, tag);
    return TRUE;
}

int main(void)
{
    char* cl = GetCommandLineA();
    /* 跳过 argv0（带引号或不带） */
    if (*cl == '"') { cl++; while (*cl && *cl != '"') cl++; if (*cl) cl++; }
    else { while (*cl && *cl != ' ') cl++; }
    while (*cl == ' ' || *cl == '\t') cl++;
    if (!*cl) {
        printf("Usage: SubProcHost <child_exe> [child_args...]\n");
        return 4;
    }

    BOOL a = RunChild(cl, FALSE, "A");
    BOOL w = RunChild(cl, TRUE, "W");
    return (a && w) ? 0 : 1;
}
