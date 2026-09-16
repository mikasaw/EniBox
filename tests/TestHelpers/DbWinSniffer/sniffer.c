/*
 * DbWinSniffer — 全系统捕获 OutputDebugString 输出（DBWIN 协议，免调试器）。
 *
 * 原理：DBWIN_BUFFER（共享内存 4B pid + 文本）+ DBWIN_BUFFER_READY（事件）
 * + DBWIN_DATA_READY（信号量）。OutputDebugString 的发送方在无调试器时
 * 会走该协议，任何先注册的监听进程即可收到。
 *
 * 用法: dbwin_sniffer.exe <持续秒数> <日志文件>
 * 退出码: 0 正常结束 / 非 0 初始化失败
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: dbwin_sniffer <seconds> <logfile>\n");
        return 2;
    }
    int seconds = atoi(argv[1]);
    if (seconds <= 0 || seconds > 7200) return 2;

    HANDLE hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, "DBWIN_BUFFER");
    if (!hMap) {
        fprintf(stderr, "sniffer: DBWIN_BUFFER create failed GLE=%lu\n", GetLastError());
        return 3;
    }
    char* buf = (char*)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    HANDLE dataReady = CreateSemaphoreA(NULL, 0, 1, "DBWIN_DATA_READY");
    HANDLE bufferReady = CreateEventA(NULL, FALSE, TRUE, "DBWIN_BUFFER_READY");
    if (!buf || !dataReady || !bufferReady) {
        fprintf(stderr, "sniffer: protocol objects failed GLE=%lu\n", GetLastError());
        return 4;
    }

    FILE* f = fopen(argv[2], "w");
    if (!f) return 5;
    fprintf(f, "sniffer start, %d s\n", seconds);
    fflush(f);

    DWORD start = GetTickCount();
    DWORD spanMs = (DWORD)seconds * 1000u;
    for (;;) {
        DWORD elapsed = GetTickCount() - start;
        if (elapsed >= spanMs) break;
        DWORD wait = spanMs - elapsed;
        if (wait > 1000) wait = 1000;
        if (WaitForSingleObject(dataReady, wait) == WAIT_OBJECT_0) {
            DWORD pid = *(DWORD*)buf;
            char* msg = buf + 4;
            DWORD now = GetTickCount() - start;
            fprintf(f, "[%lu.%03lu] pid=%lu %s\n",
                    (unsigned long)(now / 1000), (unsigned long)(now % 1000),
                    (unsigned long)pid, msg);
            fflush(f);
            SetEvent(bufferReady);
        }
    }
    fclose(f);
    return 0;
}
