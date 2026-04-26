#include "inject.h"
#include <stdlib.h>

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
    return (is_current_64bit == is_target_64bit);
}
