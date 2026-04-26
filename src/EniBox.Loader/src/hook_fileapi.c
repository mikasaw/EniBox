#include "hook_fileapi.h"
#include "vfs_runtime.h"
#include "vfs_handle.h"
#include "../deps/MinHook/include/MinHook.h"

static CreateFileW_t  g_orig_CreateFileW  = NULL;
static CreateFileA_t  g_orig_CreateFileA  = NULL;
static ReadFile_t     g_orig_ReadFile     = NULL;
static GetFileSize_t  g_orig_GetFileSize  = NULL;
static GetFileSizeEx_t g_orig_GetFileSizeEx = NULL;
static GetFileAttributesA_t g_orig_GetFileAttributesA = NULL;
static GetFileAttributesW_t g_orig_GetFileAttributesW = NULL;

HANDLE WINAPI Hook_CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSA, DWORD dwCreation, DWORD dwFlags, HANDLE hTemplate) {
    if (lpFileName) {
        VFS_FILE_ENTRY* file = VFS_LookupFileW(lpFileName);
        if (file && file->is_virtualized) {
            VFS_CONTEXT* ctx = VFS_GetContext();
            if (ctx) {
                uint32_t idx = (uint32_t)(file - ctx->files);
                VFS_FILE_HANDLE* h = VFS_HandleAlloc(idx, dwDesiredAccess, dwShareMode);
                if (h) return VFS_HandleToOsHandle(h);
            }
        }
    }
    return g_orig_CreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSA, dwCreation, dwFlags, hTemplate);
}

HANDLE WINAPI Hook_CreateFileA(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSA, DWORD dwCreation, DWORD dwFlags, HANDLE hTemplate) {
    if (lpFileName) {
        VFS_FILE_ENTRY* file = VFS_LookupFile(lpFileName);
        if (file && file->is_virtualized) {
            VFS_CONTEXT* ctx = VFS_GetContext();
            if (ctx) {
                uint32_t idx = (uint32_t)(file - ctx->files);
                VFS_FILE_HANDLE* h = VFS_HandleAlloc(idx, dwDesiredAccess, dwShareMode);
                if (h) return VFS_HandleToOsHandle(h);
            }
        }
    }
    return g_orig_CreateFileA(lpFileName, dwDesiredAccess, dwShareMode, lpSA, dwCreation, dwFlags, hTemplate);
}

BOOL WINAPI Hook_ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nBytes, LPDWORD lpBytesRead, LPOVERLAPPED lpOverlapped) {
    if (VFS_IsVirtualHandle(hFile)) {
        VFS_FILE_HANDLE* h = VFS_HandleFromOsHandle(hFile);
        if (h) {
            uint32_t br = 0;
            int32_t r = VFS_ReadFile(h, lpBuffer, nBytes, &br);
            if (lpBytesRead) *lpBytesRead = br;
            return (r == 0) ? TRUE : FALSE;
        }
    }
    return g_orig_ReadFile(hFile, lpBuffer, nBytes, lpBytesRead, lpOverlapped);
}

DWORD WINAPI Hook_GetFileSize(HANDLE hFile, LPDWORD lpHigh) {
    if (VFS_IsVirtualHandle(hFile)) {
        VFS_FILE_HANDLE* h = VFS_HandleFromOsHandle(hFile);
        if (h) { if (lpHigh) *lpHigh = 0; return VFS_GetFileSize(h); }
    }
    return g_orig_GetFileSize(hFile, lpHigh);
}

BOOL WINAPI Hook_GetFileSizeEx(HANDLE hFile, PLARGE_INTEGER lpSize) {
    if (VFS_IsVirtualHandle(hFile)) {
        VFS_FILE_HANDLE* h = VFS_HandleFromOsHandle(hFile);
        if (h && lpSize) { lpSize->QuadPart = (LONGLONG)VFS_GetFileSize(h); return TRUE; }
    }
    return g_orig_GetFileSizeEx(hFile, lpSize);
}

DWORD WINAPI Hook_GetFileAttributesA(LPCSTR lpFileName) {
    if (lpFileName) {
        VFS_FILE_ENTRY* file = VFS_LookupFile(lpFileName);
        if (file && file->is_virtualized) return file->attributes;
    }
    return g_orig_GetFileAttributesA(lpFileName);
}

DWORD WINAPI Hook_GetFileAttributesW(LPCWSTR lpFileName) {
    if (lpFileName) {
        VFS_FILE_ENTRY* file = VFS_LookupFileW(lpFileName);
        if (file && file->is_virtualized) return file->attributes;
    }
    return g_orig_GetFileAttributesW(lpFileName);
}

int32_t HookFile_Install(void) {
    if (MH_CreateHook(&CreateFileW, &Hook_CreateFileW, (LPVOID*)&g_orig_CreateFileW) != MH_OK) return -1;
    if (MH_CreateHook(&CreateFileA, &Hook_CreateFileA, (LPVOID*)&g_orig_CreateFileA) != MH_OK) return -2;
    if (MH_CreateHook(&ReadFile, &Hook_ReadFile, (LPVOID*)&g_orig_ReadFile) != MH_OK) return -3;
    if (MH_CreateHook(&GetFileSize, &Hook_GetFileSize, (LPVOID*)&g_orig_GetFileSize) != MH_OK) return -4;
    if (MH_CreateHook(&GetFileSizeEx, &Hook_GetFileSizeEx, (LPVOID*)&g_orig_GetFileSizeEx) != MH_OK) return -5;
    if (MH_CreateHook(&GetFileAttributesA, &Hook_GetFileAttributesA, (LPVOID*)&g_orig_GetFileAttributesA) != MH_OK) return -6;
    if (MH_CreateHook(&GetFileAttributesW, &Hook_GetFileAttributesW, (LPVOID*)&g_orig_GetFileAttributesW) != MH_OK) return -7;
    return 0;
}
