#ifndef INJECT_H
#define INJECT_H
#include <windows.h>
#include <stdint.h>
int32_t Inject_LoadDll(HANDLE hProcess, const wchar_t* dll_path);
BOOL Inject_ArchitectureMatches(HANDLE hProcess, BOOL is_target_64bit);
#endif
