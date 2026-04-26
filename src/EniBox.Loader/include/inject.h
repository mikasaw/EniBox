#ifndef INJECT_H
#define INJECT_H
#include <windows.h>
#include <stdint.h>

/* IsWow64Process function pointer type for dynamic loading */
typedef BOOL (WINAPI *IsWow64Process_t)(HANDLE, PBOOL);

int32_t Inject_LoadDll(HANDLE hProcess, const wchar_t* dll_path);
BOOL Inject_ArchitectureMatches(HANDLE hProcess, BOOL is_target_64bit);
#endif
