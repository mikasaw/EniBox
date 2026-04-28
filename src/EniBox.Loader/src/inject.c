#include "inject.h"
#include "loader_errors.h"
#include <stdlib.h>

typedef BOOL (WINAPI *IsWow64Process2_t)(HANDLE, USHORT*, USHORT*);

typedef LONG (NTAPI *NtCreateThreadEx_t)(
    PHANDLE ThreadHandle, ACCESS_MASK DesiredAccess,
    PVOID ObjectAttributes, HANDLE ProcessHandle,
    PVOID StartRoutine, PVOID Argument,
    ULONG CreateFlags, SIZE_T ZeroBits, SIZE_T StackSize,
    SIZE_T MaximumStackSize, PVOID AttributeList);

static int32_t InjectVia_CreateRemoteThread(HANDLE hProcess, const wchar_t* dll_path) {
    size_t path_size = (wcslen(dll_path) + 1) * sizeof(wchar_t);
    LPVOID remote_path = VirtualAllocEx(hProcess, NULL, path_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_path) return INJECT_ERR_NO_MEMORY;
    if (!WriteProcessMemory(hProcess, remote_path, dll_path, path_size, NULL)) {
        VirtualFreeEx(hProcess, remote_path, 0, MEM_RELEASE);
        return INJECT_ERR_WRITE_FAIL;
    }
    HMODULE hK32 = GetModuleHandleW(L"kernel32.dll");
    if (!hK32) { VirtualFreeEx(hProcess, remote_path, 0, MEM_RELEASE); return INJECT_ERR_NO_MODULE; }
    FARPROC pLoadLib = GetProcAddress(hK32, "LoadLibraryW");
    if (!pLoadLib) { VirtualFreeEx(hProcess, remote_path, 0, MEM_RELEASE); return INJECT_ERR_NO_FUNC; }
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)pLoadLib, remote_path, 0, NULL);
    if (!hThread) { VirtualFreeEx(hProcess, remote_path, 0, MEM_RELEASE); return INJECT_ERR_THREAD_FAIL; }
    WaitForSingleObject(hThread, 5000);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remote_path, 0, MEM_RELEASE);
    return 0;
}

static int32_t InjectVia_QueueUserAPC(HANDLE hProcess, const wchar_t* dll_path) {
    size_t path_size = (wcslen(dll_path) + 1) * sizeof(wchar_t);
    LPVOID remote_path = VirtualAllocEx(hProcess, NULL, path_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_path) return INJECT_ERR_NO_MEMORY;
    if (!WriteProcessMemory(hProcess, remote_path, dll_path, path_size, NULL)) {
        VirtualFreeEx(hProcess, remote_path, 0, MEM_RELEASE);
        return INJECT_ERR_WRITE_FAIL;
    }
    HMODULE hK32 = GetModuleHandleW(L"kernel32.dll");
    if (!hK32) { VirtualFreeEx(hProcess, remote_path, 0, MEM_RELEASE); return INJECT_ERR_NO_MODULE; }
    FARPROC pLoadLib = GetProcAddress(hK32, "LoadLibraryW");
    if (!pLoadLib) { VirtualFreeEx(hProcess, remote_path, 0, MEM_RELEASE); return INJECT_ERR_NO_FUNC; }

    DWORD result = QueueUserAPC((PAPCFUNC)pLoadLib, hProcess, (ULONG_PTR)remote_path);
    if (result == 0) {
        VirtualFreeEx(hProcess, remote_path, 0, MEM_RELEASE);
        return INJECT_ERR_APC_FAIL;
    }

    OutputDebugStringW(L"[EniBox] InjectVia_QueueUserAPC: queued successfully");
    return 0;
}

static int32_t InjectVia_NtCreateThreadEx(HANDLE hProcess, const wchar_t* dll_path) {
    size_t path_size = (wcslen(dll_path) + 1) * sizeof(wchar_t);
    LPVOID remote_path = VirtualAllocEx(hProcess, NULL, path_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_path) return INJECT_ERR_NO_MEMORY;
    if (!WriteProcessMemory(hProcess, remote_path, dll_path, path_size, NULL)) {
        VirtualFreeEx(hProcess, remote_path, 0, MEM_RELEASE);
        return INJECT_ERR_WRITE_FAIL;
    }
    HMODULE hK32 = GetModuleHandleW(L"kernel32.dll");
    if (!hK32) { VirtualFreeEx(hProcess, remote_path, 0, MEM_RELEASE); return INJECT_ERR_NO_MODULE; }
    FARPROC pLoadLib = GetProcAddress(hK32, "LoadLibraryW");
    if (!pLoadLib) { VirtualFreeEx(hProcess, remote_path, 0, MEM_RELEASE); return INJECT_ERR_NO_FUNC; }

    HMODULE hNtDll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtDll) { VirtualFreeEx(hProcess, remote_path, 0, MEM_RELEASE); return INJECT_ERR_NO_MODULE; }

    NtCreateThreadEx_t pNtCreateThreadEx = (NtCreateThreadEx_t)
        GetProcAddress(hNtDll, "NtCreateThreadEx");
    if (!pNtCreateThreadEx) { VirtualFreeEx(hProcess, remote_path, 0, MEM_RELEASE); return INJECT_ERR_NTCREATE_FAIL; }

    HANDLE hThread = NULL;
    LONG status = pNtCreateThreadEx(&hThread, THREAD_ALL_ACCESS, NULL, hProcess,
        (PVOID)pLoadLib, remote_path, 0, 0, 0, 0, NULL);
    if (status != 0 || !hThread) {
        VirtualFreeEx(hProcess, remote_path, 0, MEM_RELEASE);
        return INJECT_ERR_NTCREATE_FAIL;
    }

    WaitForSingleObject(hThread, 5000);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remote_path, 0, MEM_RELEASE);

    OutputDebugStringW(L"[EniBox] InjectVia_NtCreateThreadEx: created thread successfully");
    return 0;
}

InjectMethod Inject_DetectBestMethod(HANDLE hProcess) {
    /* APC is least detectable but requires target to have alertable threads */
    /* For suspended processes (our use case), APC works well */
    if (hProcess != NULL) {
        return INJECT_METHOD_APC;
    }
    return INJECT_METHOD_CREATE_REMOTE_THREAD;
}

int32_t Inject_LoadDllEx(HANDLE hProcess, const wchar_t* dll_path, InjectMethod preferred_method) {
    if (!hProcess || !dll_path) return INJECT_ERR_INVALID_PARAM;

    int32_t result = -1;

    switch (preferred_method) {
    case INJECT_METHOD_APC:
        result = InjectVia_QueueUserAPC(hProcess, dll_path);
        if (result == 0) {
            OutputDebugStringW(L"[EniBox] Injection succeeded via QueueUserAPC");
            return 0;
        }
        OutputDebugStringW(L"[EniBox] APC injection failed, falling back to NtCreateThreadEx");
        result = InjectVia_NtCreateThreadEx(hProcess, dll_path);
        if (result == 0) {
            OutputDebugStringW(L"[EniBox] Injection succeeded via NtCreateThreadEx");
            return 0;
        }
        OutputDebugStringW(L"[EniBox] NtCreateThreadEx failed, falling back to CreateRemoteThread");
        return InjectVia_CreateRemoteThread(hProcess, dll_path);

    case INJECT_METHOD_NT_CREATE_THREAD:
        result = InjectVia_NtCreateThreadEx(hProcess, dll_path);
        if (result == 0) {
            OutputDebugStringW(L"[EniBox] Injection succeeded via NtCreateThreadEx");
            return 0;
        }
        OutputDebugStringW(L"[EniBox] NtCreateThreadEx failed, falling back to CreateRemoteThread");
        return InjectVia_CreateRemoteThread(hProcess, dll_path);

    case INJECT_METHOD_CREATE_REMOTE_THREAD:
    default:
        return InjectVia_CreateRemoteThread(hProcess, dll_path);
    }
}

int32_t Inject_LoadDll(HANDLE hProcess, const wchar_t* dll_path) {
    InjectMethod method = Inject_DetectBestMethod(hProcess);
    return Inject_LoadDllEx(hProcess, dll_path, method);
}

BOOL Inject_ArchitectureMatches(HANDLE hProcess, BOOL is_target_64bit) {
    BOOL is_current_64bit = FALSE;
#ifdef _WIN64
    is_current_64bit = TRUE;
#endif

    if (!hProcess) return (is_current_64bit == is_target_64bit);

    HMODULE hK32 = GetModuleHandleW(L"kernel32.dll");
    if (hK32) {
        IsWow64Process2_t pIsWow64Process2 = (IsWow64Process2_t)
            GetProcAddress(hK32, "IsWow64Process2");
        if (pIsWow64Process2) {
            USHORT processMachine = IMAGE_FILE_MACHINE_UNKNOWN;
            USHORT nativeMachine = IMAGE_FILE_MACHINE_UNKNOWN;
            if (pIsWow64Process2(hProcess, &processMachine, &nativeMachine)) {
                if (processMachine == IMAGE_FILE_MACHINE_UNKNOWN) {
                    BOOL targetIs64 = (nativeMachine == IMAGE_FILE_MACHINE_AMD64);
                    return (is_current_64bit == targetIs64);
                } else {
                    BOOL targetIs64 = (processMachine == IMAGE_FILE_MACHINE_AMD64);
                    return (is_current_64bit == targetIs64);
                }
            }
        }
    }

    BOOL isWow64 = FALSE;
    IsWow64Process_t pIsWow64Process = (IsWow64Process_t)
        GetProcAddress(hK32, "IsWow64Process");
    if (pIsWow64Process && pIsWow64Process(hProcess, &isWow64)) {
        if (isWow64) {
            return (is_current_64bit == FALSE);
        }
        return (is_current_64bit == is_target_64bit);
    }

    return (is_current_64bit == is_target_64bit);
}
