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
#include "vfs_link.h"
#include "diag_file.h"
#include "../deps/MinHook/include/MinHook.h"
#include <stdlib.h>
#include <stdio.h>
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

/* Parent VFS readiness: only hand the VFS to children when this process
 * actually initialized one (set by loader_main after Loader_Initialize). */
static BOOL g_vfs_ready = FALSE;

void HookProcess_SetVfsReady(BOOL ready) { g_vfs_ready = ready; }

/* Packed config flags, propagated to injected children via VfsLink. */
static uint32_t g_config_flags = 0;

void HookProcess_SetConfigFlags(uint32_t flags) { g_config_flags = flags; }

/* ---- Child image inspection + VfsLink handoff ---- */

/* Detect a packed child: its image carries a .enibox section, so it
 * bootstraps its own Loader. Injecting ours into it would shadow the
 * child's own module by name and break the child's VFS initialization. */
static BOOL ChildHasEniboxSectionW(LPCWSTR exePath) {
    HANDLE hFile = CreateFileW(exePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                               NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart <= 0) {
        CloseHandle(hFile);
        return FALSE;
    }
    HANDLE hMap = CreateFileMappingW(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    CloseHandle(hFile);
    if (!hMap) return FALSE;
    const uint8_t* base = (const uint8_t*)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (!base) { CloseHandle(hMap); return FALSE; }

    BOOL hasSection = FALSE;
    const uint64_t size = (uint64_t)fileSize.QuadPart;
    do {
        if (size < sizeof(IMAGE_DOS_HEADER)) break;
        IMAGE_DOS_HEADER dos;
        memcpy(&dos, base, sizeof(dos));
        if (dos.e_magic != 0x5A4D) break;
        if (dos.e_lfanew <= 0 || (uint64_t)dos.e_lfanew + sizeof(IMAGE_NT_HEADERS64) > size) break;
        IMAGE_NT_HEADERS64 nt;
        memcpy(&nt, base + dos.e_lfanew, sizeof(nt));
        if (nt.Signature != 0x00004550 || nt.FileHeader.Machine != 0x8664) break;
        if (nt.FileHeader.NumberOfSections == 0 || nt.FileHeader.NumberOfSections > 96) break;
        const uint8_t* sectab = base + dos.e_lfanew + sizeof(DWORD) +
            sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader;
        if ((uint64_t)(sectab - base) + (uint64_t)nt.FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER) > size)
            break;
        for (uint16_t i = 0; i < nt.FileHeader.NumberOfSections; i++) {
            IMAGE_SECTION_HEADER sec;
            memcpy(&sec, sectab + (size_t)i * sizeof(IMAGE_SECTION_HEADER), sizeof(sec));
            if (memcmp(sec.Name, ".enibox", 8) == 0) { hasSection = TRUE; break; }
        }
    } while (0);
    UnmapViewOfFile(base);
    CloseHandle(hMap);
    return hasSection;
}

/* Resolve the child image path: lpApplicationName when present, otherwise
 * the first token of the command line (quoted or bare). */
static BOOL ResolveChildImageW(LPCWSTR lpApplicationName, LPCWSTR lpCommandLine,
                               wchar_t* exePath, DWORD cch) {
    if (lpApplicationName) {
        wcsncpy_s(exePath, cch, lpApplicationName, _TRUNCATE);
        return TRUE;
    }
    if (!lpCommandLine) return FALSE;
    const wchar_t* cmd = lpCommandLine;
    size_t len;
    if (cmd[0] == L'"') {
        const wchar_t* end = wcschr(cmd + 1, L'"');
        if (!end) return FALSE;
        len = (size_t)(end - (cmd + 1));
        if (len >= cch) return FALSE;
        wcsncpy_s(exePath, cch, cmd + 1, (uint32_t)len);
        return TRUE;
    }
    const wchar_t* sp = wcschr(cmd, L' ');
    len = sp ? (size_t)(sp - cmd) : wcslen(cmd);
    if (len == 0 || len >= cch) return FALSE;
    wcsncpy_s(exePath, cch, cmd, (uint32_t)len);
    return TRUE;
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

/* Write the VfsLink handoff file next to the (extracted) Loader DLL.
 * The injected Loader reads it in its DllMain to find the parent image
 * whose .enibox section carries the VFS. */
static BOOL WriteVfsLinkFile(void) {
    if (!g_vfs_ready || !g_loader_path_set) return FALSE;
    wchar_t dir[MAX_PATH];
    wcsncpy_s(dir, MAX_PATH, g_loader_path, _TRUNCATE);
    wchar_t* slash = wcsrchr(dir, L'\\');
    if (!slash) return FALSE;
    *slash = L'\0';
    if (dir[0] == L'\0') return FALSE;

    wchar_t linkPath[MAX_PATH];
    if (swprintf_s(linkPath, MAX_PATH, L"%s\\%s", dir, ENIBOX_VFSLINK_NAME) < 0) return FALSE;

    wchar_t image[MAX_PATH];
    if (!GetModuleFileNameW(NULL, image, MAX_PATH)) return FALSE;
    const uint32_t imageChars = (uint32_t)wcslen(image);
    if (imageChars == 0) return FALSE;

    HANDLE h = CreateFileW(linkPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    BOOL ok = FALSE;
    DWORD written = 0;
    const uint32_t magic = ENIBOX_VFSLINK_MAGIC;
    ok = WriteFile(h, &magic, sizeof(magic), &written, NULL) && written == sizeof(magic);
    if (ok) ok = WriteFile(h, &imageChars, sizeof(imageChars), &written, NULL) && written == sizeof(imageChars);
    if (ok) ok = WriteFile(h, image, imageChars * sizeof(wchar_t), &written, NULL)
                       && written == imageChars * sizeof(wchar_t);
    if (ok) ok = WriteFile(h, &g_config_flags, sizeof(g_config_flags), &written, NULL)
                       && written == sizeof(g_config_flags);
    CloseHandle(h);
    return ok;
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

    /* Packed children bootstrap their own Loader - injecting ours would
     * shadow the child's module by name and break its VFS init. */
    wchar_t childImage[MAX_PATH];
    if (ResolveChildImageW(lpApplicationName, lpCommandLine, childImage, MAX_PATH)
        && ChildHasEniboxSectionW(childImage)) {
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
        /* The VfsLink must exist before the injected Loader's DllMain runs */
        BOOL linkOk = WriteVfsLinkFile();
        int32_t injectResult = -1;
        if (linkOk) {
            injectResult = Inject_LoadDll(lpProcessInformation->hProcess, g_loader_path);
        }
        {
            char d[128];
            sprintf_s(d, sizeof(d), "inject W pid=%lu link=%d inj=%d",
                      lpProcessInformation->dwProcessId, linkOk ? 1 : 0, injectResult);
            EniBox_DiagLine(d);
        }
        ResumeThread(lpProcessInformation->hThread);
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

    /* Packed children bootstrap their own Loader (see the W detour) */
    wchar_t childImage[MAX_PATH];
    if (ResolveChildImageW(wideAppName[0] ? wideAppName : NULL, wideCmdLine,
                           childImage, MAX_PATH)
        && ChildHasEniboxSectionW(childImage)) {
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
        /* The VfsLink must exist before the injected Loader's DllMain runs */
        BOOL linkOk = WriteVfsLinkFile();
        int32_t injectResult = -1;
        if (linkOk) {
            injectResult = Inject_LoadDll(lpProcessInformation->hProcess, g_loader_path);
        }
        {
            char d[128];
            sprintf_s(d, sizeof(d), "inject A pid=%lu link=%d inj=%d",
                      lpProcessInformation->dwProcessId, linkOk ? 1 : 0, injectResult);
            EniBox_DiagLine(d);
        }
        ResumeThread(lpProcessInformation->hThread);
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
