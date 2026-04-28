#ifndef INJECT_H
#define INJECT_H
#include <windows.h>
#include <stdint.h>

typedef BOOL (WINAPI *IsWow64Process_t)(HANDLE, PBOOL);

typedef enum {
    INJECT_METHOD_APC = 1,
    INJECT_METHOD_NT_CREATE_THREAD = 2,
    INJECT_METHOD_CREATE_REMOTE_THREAD = 3
} InjectMethod;

int32_t Inject_LoadDll(HANDLE hProcess, const wchar_t* dll_path);
int32_t Inject_LoadDllEx(HANDLE hProcess, const wchar_t* dll_path, InjectMethod preferred_method);
InjectMethod Inject_DetectBestMethod(HANDLE hProcess);
BOOL Inject_ArchitectureMatches(HANDLE hProcess, BOOL is_target_64bit);
#endif
