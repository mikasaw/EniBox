/*
 * Process Creation Hooks - CreateProcessA/W
 *
 * These hooks intercept child process creation to inject the Loader DLL
 * into newly spawned processes. This ensures that VFS virtualization
 * propagates to all child processes, maintaining the virtual filesystem
 * illusion across the entire process tree.
 *
 * Strategy:
 * 1. Hook CreateProcessA/W
 * 2. When a child process is created, add CREATE_SUSPENDED flag
 * 3. After process creation, inject Loader DLL via Inject_LoadDll
 * 4. Resume the child process main thread
 */

#include "hook_process.h"
#include "inject.h"
#include "../deps/MinHook/include/MinHook.h"
#include <stdlib.h>
#include <string.h>

/* ---- Original function pointers ---- */

typedef BOOL (WINAPI *CreateProcessA_t)(LPCSTR, LPSTR, LPSECURITY_ATTRIBUTES,
    LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCSTR, LPSTARTUPINFOA, LPPROCESS_INFORMATION);
typedef BOOL (WINAPI *CreateProcessW_t)(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES,
    LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION);

static CreateProcessA_t g_orig_CreateProcessA = NULL;
static CreateProcessW_t g_orig_CreateProcessW = NULL;

/* ---- Loader DLL path ---- */

static wchar_t g_loader_path[MAX_PATH] = {0};
static BOOL g_loader_path_set = FALSE;

const wchar_t* HookProcess_GetLoaderPath(void) {
    return g_loader_path_set ? g_loader_path : NULL;
}

void HookProcess_SetLoaderPath(const wchar_t* path) {
    if (path) {
        wcsncpy_s(g_loader_path, MAX_PATH, path, MAX_PATH - 1);
        g_loader_path_set = TRUE;
    }
}

/* ---- Helper: Determine if process should be injected ---- */

static BOOL ShouldInjectProcess(LPCWSTR lpApplicationName, LPCWSTR lpCommandLine) {
    /* We inject into all child processes to maintain VFS consistency.
     * Skip injection for known system processes that don't need VFS. */
    static const wchar_t* skipList[] = {
        L"\\system32\\",
        L"\\SysWOW64\\",
        L"conhost.exe",
        L"cmd.exe",
        L"werfault.exe",
        NULL
    };

    const wchar_t* checkPath = lpApplicationName ? lpApplicationName : lpCommandLine;
    if (!checkPath) return TRUE;

    wchar_t lowerPath[MAX_PATH];
    wcsncpy_s(lowerPath, MAX_PATH, checkPath, MAX_PATH - 1);
    for (uint32_t i = 0; lowerPath[i]; i++)
        if (lowerPath[i] >= L'A' && lowerPath[i] <= L'Z')
            lowerPath[i] = (wchar_t)(lowerPath[i] + 32);

    for (int i = 0; skipList[i]; i++) {
        if (wcsstr(lowerPath, skipList[i]))
            return FALSE;
    }

    return TRUE;
}

/* ---- Hook implementations ---- */

static BOOL WINAPI Hook_CreateProcessW(
    LPCWSTR lpApplicationName, LPWSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes,
    LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles, DWORD dwCreationFlags,
    LPVOID lpEnvironment, LPCWSTR lpCurrentDirectory,
    LPSTARTUPINFOW lpStartupInfo,
    LPPROCESS_INFORMATION lpProcessInformation)
{
    if (!g_loader_path_set || !ShouldInjectProcess(lpApplicationName, lpCommandLine)) {
        return g_orig_CreateProcessW(lpApplicationName, lpCommandLine,
            lpProcessAttributes, lpThreadAttributes, bInheritHandles,
            dwCreationFlags, lpEnvironment, lpCurrentDirectory,
            lpStartupInfo, lpProcessInformation);
    }

    /* Create the child process in suspended state so we can inject */
    DWORD modifiedFlags = dwCreationFlags | CREATE_SUSPENDED;

    BOOL result = g_orig_CreateProcessW(lpApplicationName, lpCommandLine,
        lpProcessAttributes, lpThreadAttributes, bInheritHandles,
        modifiedFlags, lpEnvironment, lpCurrentDirectory,
        lpStartupInfo, lpProcessInformation);

    if (result && lpProcessInformation) {
        /* Inject Loader DLL into the child process */
        int32_t injectResult = Inject_LoadDll(lpProcessInformation->hProcess, g_loader_path);

        if (injectResult == 0) {
            /* Injection succeeded - resume the main thread */
            ResumeThread(lpProcessInformation->hThread);
        } else {
            /* Injection failed - still resume so the process can run
             * (it just won't have VFS virtualization) */
            ResumeThread(lpProcessInformation->hThread);
        }
    }

    return result;
}

static BOOL WINAPI Hook_CreateProcessA(
    LPCSTR lpApplicationName, LPSTR lpCommandLine,
    LPSECURITY_ATTRIBUTES lpProcessAttributes,
    LPSECURITY_ATTRIBUTES lpThreadAttributes,
    BOOL bInheritHandles, DWORD dwCreationFlags,
    LPVOID lpEnvironment, LPCSTR lpCurrentDirectory,
    LPSTARTUPINFOA lpStartupInfo,
    LPPROCESS_INFORMATION lpProcessInformation)
{
    if (!g_loader_path_set) {
        return g_orig_CreateProcessA(lpApplicationName, lpCommandLine,
            lpProcessAttributes, lpThreadAttributes, bInheritHandles,
            dwCreationFlags, lpEnvironment, lpCurrentDirectory,
            lpStartupInfo, lpProcessInformation);
    }

    /* Convert A strings to W for the ShouldInjectProcess check */
    wchar_t wideAppName[MAX_PATH] = {0};
    wchar_t wideCmdLine[1024] = {0};
    if (lpApplicationName)
        MultiByteToWideChar(CP_ACP, 0, lpApplicationName, -1, wideAppName, MAX_PATH);
    if (lpCommandLine)
        MultiByteToWideChar(CP_ACP, 0, lpCommandLine, -1, wideCmdLine, 1024);

    if (!ShouldInjectProcess(wideAppName, wideCmdLine)) {
        return g_orig_CreateProcessA(lpApplicationName, lpCommandLine,
            lpProcessAttributes, lpThreadAttributes, bInheritHandles,
            dwCreationFlags, lpEnvironment, lpCurrentDirectory,
            lpStartupInfo, lpProcessInformation);
    }

    /* Create the child process in suspended state */
    DWORD modifiedFlags = dwCreationFlags | CREATE_SUSPENDED;

    BOOL result = g_orig_CreateProcessA(lpApplicationName, lpCommandLine,
        lpProcessAttributes, lpThreadAttributes, bInheritHandles,
        modifiedFlags, lpEnvironment, lpCurrentDirectory,
        lpStartupInfo, lpProcessInformation);

    if (result && lpProcessInformation) {
        /* Inject Loader DLL into the child process */
        int32_t injectResult = Inject_LoadDll(lpProcessInformation->hProcess, g_loader_path);

        if (injectResult == 0) {
            ResumeThread(lpProcessInformation->hThread);
        } else {
            ResumeThread(lpProcessInformation->hThread);
        }
    }

    return result;
}

/* ---- Installation ---- */

int32_t HookProcess_Install(void) {
    if (MH_CreateHook(&CreateProcessW, &Hook_CreateProcessW,
        (void**)&g_orig_CreateProcessW) != MH_OK) return -1;
    if (MH_CreateHook(&CreateProcessA, &Hook_CreateProcessA,
        (void**)&g_orig_CreateProcessA) != MH_OK) return -2;
    return 0;
}
