#include "hook_fileapi.h"
#include "vfs_runtime.h"
#include "vfs_handle.h"
#include "../deps/MinHook/include/MinHook.h"

static CreateFileW_t  g_orig_CreateFileW  = NULL;
static CreateFileA_t  g_orig_CreateFileA  = NULL;
static ReadFile_t     g_orig_ReadFile     = NULL;
static WriteFile_t    g_orig_WriteFile    = NULL;
static GetFileSize_t  g_orig_GetFileSize  = NULL;
static GetFileSizeEx_t g_orig_GetFileSizeEx = NULL;
static GetFileAttributesA_t g_orig_GetFileAttributesA = NULL;
static GetFileAttributesW_t g_orig_GetFileAttributesW = NULL;
static SetFilePointer_t g_orig_SetFilePointer = NULL;
static SetFilePointerEx_t g_orig_SetFilePointerEx = NULL;

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

BOOL WINAPI Hook_WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nBytes,
    LPDWORD lpBytesWritten, LPOVERLAPPED lpOverlapped) {
    /* VFS is read-only - reject writes to virtual handles */
    if (VFS_IsVirtualHandle(hFile)) {
        SetLastError(ERROR_ACCESS_DENIED);
        if (lpBytesWritten) *lpBytesWritten = 0;
        return FALSE;
    }
    return g_orig_WriteFile(hFile, lpBuffer, nBytes, lpBytesWritten, lpOverlapped);
}

/* Note: CloseHandle is hooked in hook_filemapping.c which handles
 * VFS virtual handles, find handles, AND mapping handles.
 * Do NOT hook CloseHandle here to avoid MinHook conflict. */

DWORD WINAPI Hook_SetFilePointer(HANDLE hFile, LONG lDistanceToMove,
    PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod) {
    /* Route file seek operations for virtual handles to VFS */
    if (VFS_IsVirtualHandle(hFile)) {
        VFS_FILE_HANDLE* h = VFS_HandleFromOsHandle(hFile);
        if (h) {
            uint32_t file_size = VFS_GetFileSize(h);
            uint32_t new_pos;

            switch (dwMoveMethod) {
            case FILE_BEGIN:
                new_pos = (uint32_t)lDistanceToMove;
                break;
            case FILE_CURRENT:
                new_pos = (uint32_t)((uint64_t)h->current_pos + (int64_t)lDistanceToMove);
                break;
            case FILE_END:
                new_pos = (uint32_t)((uint64_t)file_size + (int64_t)lDistanceToMove);
                break;
            default:
                SetLastError(ERROR_INVALID_PARAMETER);
                return INVALID_SET_FILE_POINTER;
            }

            /* Clamp to file size */
            if (new_pos > file_size) new_pos = file_size;
            h->current_pos = new_pos;

            if (lpDistanceToMoveHigh) *lpDistanceToMoveHigh = 0;
            return new_pos;
        }
        SetLastError(ERROR_INVALID_HANDLE);
        return INVALID_SET_FILE_POINTER;
    }
    return g_orig_SetFilePointer(hFile, lDistanceToMove, lpDistanceToMoveHigh, dwMoveMethod);
}

BOOL WINAPI Hook_SetFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistanceToMove,
    PLARGE_INTEGER lpNewFilePointer, DWORD dwMoveMethod) {
    /* Route 64-bit file seek operations for virtual handles to VFS */
    if (VFS_IsVirtualHandle(hFile)) {
        VFS_FILE_HANDLE* h = VFS_HandleFromOsHandle(hFile);
        if (h) {
            uint32_t file_size = VFS_GetFileSize(h);
            int64_t offset = liDistanceToMove.QuadPart;
            uint32_t new_pos;

            switch (dwMoveMethod) {
            case FILE_BEGIN:
                new_pos = (offset < 0) ? 0 : (uint32_t)offset;
                break;
            case FILE_CURRENT:
                new_pos = (uint32_t)((int64_t)h->current_pos + offset);
                break;
            case FILE_END:
                new_pos = (uint32_t)((int64_t)file_size + offset);
                break;
            default:
                SetLastError(ERROR_INVALID_PARAMETER);
                return FALSE;
            }

            /* Clamp to file size */
            if (new_pos > file_size) new_pos = file_size;
            h->current_pos = new_pos;

            if (lpNewFilePointer) lpNewFilePointer->QuadPart = (LONGLONG)new_pos;
            return TRUE;
        }
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return g_orig_SetFilePointerEx(hFile, liDistanceToMove, lpNewFilePointer, dwMoveMethod);
}

int32_t HookFile_Install(void) {
    if (MH_CreateHook(&CreateFileW, &Hook_CreateFileW, (LPVOID*)&g_orig_CreateFileW) != MH_OK) return -1;
    if (MH_CreateHook(&CreateFileA, &Hook_CreateFileA, (LPVOID*)&g_orig_CreateFileA) != MH_OK) return -2;
    if (MH_CreateHook(&ReadFile, &Hook_ReadFile, (LPVOID*)&g_orig_ReadFile) != MH_OK) return -3;
    if (MH_CreateHook(&GetFileSize, &Hook_GetFileSize, (LPVOID*)&g_orig_GetFileSize) != MH_OK) return -4;
    if (MH_CreateHook(&GetFileSizeEx, &Hook_GetFileSizeEx, (LPVOID*)&g_orig_GetFileSizeEx) != MH_OK) return -5;
    if (MH_CreateHook(&GetFileAttributesA, &Hook_GetFileAttributesA, (LPVOID*)&g_orig_GetFileAttributesA) != MH_OK) return -6;
    if (MH_CreateHook(&GetFileAttributesW, &Hook_GetFileAttributesW, (LPVOID*)&g_orig_GetFileAttributesW) != MH_OK) return -7;
    if (MH_CreateHook(&WriteFile, &Hook_WriteFile, (LPVOID*)&g_orig_WriteFile) != MH_OK) return -8;
    /* CloseHandle is hooked in hook_filemapping.c - do not hook here */
    if (MH_CreateHook(&SetFilePointer, &Hook_SetFilePointer, (LPVOID*)&g_orig_SetFilePointer) != MH_OK) return -10;
    if (MH_CreateHook(&SetFilePointerEx, &Hook_SetFilePointerEx, (LPVOID*)&g_orig_SetFilePointerEx) != MH_OK) return -11;
    return 0;
}
