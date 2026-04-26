#include "inject.h"
#include <stdlib.h>

/* Forward declaration for IsWow64Process2 (available on Windows 10+) */
typedef BOOL (WINAPI *IsWow64Process2_t)(HANDLE, USHORT*, USHORT*);

int32_t Inject_LoadDll(HANDLE hProcess, const wchar_t* dll_path) {
    if (!hProcess || !dll_path) return -1;
    size_t path_size = (wcslen(dll_path) + 1) * sizeof(wchar_t);
    LPVOID remote_path = VirtualAllocEx(hProcess, NULL, path_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_path) return -2;
    if (!WriteProcessMemory(hProcess, remote_path, dll_path, path_size, NULL)) {
        VirtualFreeEx(hProcess, remote_path, 0, MEM_RELEASE); return -3;
    }
    HMODULE hK32 = GetModuleHandleW(L"kernel32.dll");
    if (!hK32) { VirtualFreeEx(hProcess, remote_path, 0, MEM_RELEASE); return -4; }
    FARPROC pLoadLib = GetProcAddress(hK32, "LoadLibraryW");
    if (!pLoadLib) { VirtualFreeEx(hProcess, remote_path, 0, MEM_RELEASE); return -5; }
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)pLoadLib, remote_path, 0, NULL);
    if (!hThread) { VirtualFreeEx(hProcess, remote_path, 0, MEM_RELEASE); return -6; }
    WaitForSingleObject(hThread, 5000);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remote_path, 0, MEM_RELEASE);
    return 0;
}

BOOL Inject_ArchitectureMatches(HANDLE hProcess, BOOL is_target_64bit) {
    BOOL is_current_64bit = FALSE;
#ifdef _WIN64
    is_current_64bit = TRUE;
#endif

    /* If no process handle, fall back to simple comparison */
    if (!hProcess) return (is_current_64bit == is_target_64bit);

    /* Use IsWow64Process2 (Windows 10+) for accurate detection.
     * This correctly handles WoW64 scenarios where a 32-bit process
     * runs on a 64-bit system. */
    HMODULE hK32 = GetModuleHandleW(L"kernel32.dll");
    if (hK32) {
        IsWow64Process2_t pIsWow64Process2 = (IsWow64Process2_t)
            GetProcAddress(hK32, "IsWow64Process2");
        if (pIsWow64Process2) {
            USHORT processMachine = IMAGE_FILE_MACHINE_UNKNOWN;
            USHORT nativeMachine = IMAGE_FILE_MACHINE_UNKNOWN;
            if (pIsWow64Process2(hProcess, &processMachine, &nativeMachine)) {
                /* processMachine == IMAGE_FILE_MACHINE_UNKNOWN means native process
                 * (not running under WoW64) */
                if (processMachine == IMAGE_FILE_MACHINE_UNKNOWN) {
                    /* Native process - use nativeMachine to determine architecture */
                    BOOL targetIs64 = (nativeMachine == IMAGE_FILE_MACHINE_AMD64);
                    return (is_current_64bit == targetIs64);
                } else {
                    /* WoW64 process - processMachine tells us the actual architecture */
                    BOOL targetIs64 = (processMachine == IMAGE_FILE_MACHINE_AMD64);
                    return (is_current_64bit == targetIs64);
                }
            }
        }
    }

    /* Fallback: use IsWow64Process (available since Windows XP) */
    BOOL isWow64 = FALSE;
    IsWow64Process_t pIsWow64Process = (IsWow64Process_t)
        GetProcAddress(hK32, "IsWow64Process");
    if (pIsWow64Process && pIsWow64Process(hProcess, &isWow64)) {
        if (isWow64) {
            /* Target is a 32-bit process running under WoW64 on 64-bit OS */
            return (is_current_64bit == FALSE);
        }
        /* Not WoW64 - target matches OS architecture */
        return (is_current_64bit == is_target_64bit);
    }

    /* Final fallback: simple comparison */
    return (is_current_64bit == is_target_64bit);
}
