/*
 * FindFile Hooks - FindFirstFileA/W, FindNextFileA/W
 * 
 * These hooks enable directory enumeration within the virtual file system.
 * When an application calls FindFirstFile/FindNextFile on a virtual directory,
 * we enumerate the matching files from the VFS instead of the real filesystem.
 */

#include "hook_findfile.h"
#include "vfs_runtime.h"
#include "vfs_handle.h"
#include "../deps/MinHook/include/MinHook.h"
#include <stdlib.h>
#include <string.h>

/* ---- Original function pointers ---- */

typedef HANDLE (WINAPI *FindFirstFileW_t)(LPCWSTR, LPWIN32_FIND_DATAW);
typedef HANDLE (WINAPI *FindFirstFileA_t)(LPCSTR, LPWIN32_FIND_DATAA);
typedef BOOL   (WINAPI *FindNextFileW_t)(HANDLE, LPWIN32_FIND_DATAW);
typedef BOOL   (WINAPI *FindNextFileA_t)(HANDLE, LPWIN32_FIND_DATAA);

static FindFirstFileW_t g_orig_FindFirstFileW = NULL;
static FindFirstFileA_t g_orig_FindFirstFileA = NULL;
static FindNextFileW_t  g_orig_FindNextFileW  = NULL;
static FindNextFileA_t  g_orig_FindNextFileA  = NULL;

/* ---- Virtual find handle management ---- */

static VFIND_HANDLE g_findHandles[MAX_FIND_HANDLES];

VFIND_HANDLE* HookFind_GetHandle(HANDLE h) {
    uintptr_t val = (uintptr_t)h;
    if (val < FIND_HANDLE_BASE || val >= FIND_HANDLE_BASE + MAX_FIND_HANDLES)
        return NULL;
    uint32_t idx = (uint32_t)(val - FIND_HANDLE_BASE);
    if (!g_findHandles[idx].inUse) return NULL;
    return &g_findHandles[idx];
}

static HANDLE AllocFindHandle(void) {
    for (uint32_t i = 0; i < MAX_FIND_HANDLES; i++) {
        if (!g_findHandles[i].inUse) {
            g_findHandles[i].inUse = TRUE;
            return (HANDLE)(uintptr_t)(FIND_HANDLE_BASE + i);
        }
    }
    return INVALID_HANDLE_VALUE;
}

void HookFind_FreeHandle(HANDLE h) {
    VFIND_HANDLE* fh = HookFind_GetHandle(h);
    if (fh) {
        fh->inUse = FALSE;
        if (fh->matchIndices) { free(fh->matchIndices); fh->matchIndices = NULL; }
        fh->matchCount = 0;
        fh->matchPos = 0;
    }
}

/* ---- Helper: Check if a path is in the VFS ---- */

static BOOL IsVirtualDirectory(const char* path) {
    VFS_CONTEXT* ctx = VFS_GetContext();
    if (!ctx || !ctx->header) return FALSE;

    /* Check if any file in VFS starts with this directory path */
    char normalized[260];
    strncpy_s(normalized, 260, path, 259);
    VFS_NormalizePath(normalized, 260);

    /* Remove trailing wildcard for directory check */
    char dirPath[260];
    strncpy_s(dirPath, 260, normalized, 259);
    char* star = strchr(dirPath, '*');
    if (star) *star = '\0';
    /* Remove trailing backslash */
    size_t len = strlen(dirPath);
    if (len > 0 && dirPath[len - 1] == '\\') dirPath[len - 1] = '\0';

    /* Search hash table for any file in this directory */
    for (uint32_t i = 0; i < ctx->header->file_count; i++) {
        VFS_HASH_ENTRY* entry = NULL;
        /* We need to check if any file's path starts with dirPath */
        /* Since we can't iterate the hash table efficiently, check file entries */
        VFS_FILE_ENTRY* file = &ctx->files[i];
        const char* fileName = ctx->string_pool + file->name_offset;

        /* Build full path for this file */
        char fullPath[260] = {0};
        uint32_t dirIdx = file->dir_index;
        char components[16][260];
        int compCount = 0;
        strncpy_s(components[compCount++], 260, fileName, 259);
        while (dirIdx != VFS_INVALID_INDEX && dirIdx < ctx->header->dir_count && compCount < 16) {
            VFS_DIR_ENTRY* dir = &ctx->dirs[dirIdx];
            strncpy_s(components[compCount++], 260, ctx->string_pool + dir->name_offset, 259);
            dirIdx = dir->parent_index;
        }
        for (int c = compCount - 1; c >= 0; c--) {
            if (c < compCount - 1) strcat_s(fullPath, 260, "\\");
            strcat_s(fullPath, 260, components[c]);
        }
        VFS_NormalizePath(fullPath, 260);

        /* Check if this file is in the target directory */
        if (strncmp(fullPath, dirPath, strlen(dirPath)) == 0)
            return TRUE;
    }

    return FALSE;
}

/* ---- Helper: Simple wildcard match ---- */

static BOOL WildcardMatch(const char* pattern, const char* name) {
    if (!pattern || !name) return FALSE;
    if (strcmp(pattern, "*") == 0 || strcmp(pattern, "*.*") == 0) return TRUE;

    const char* p = pattern;
    const char* n = name;

    while (*p && *n) {
        if (*p == '*') {
            p++;
            if (*p == '\0') return TRUE;
            while (*n) {
                if (WildcardMatch(p, n)) return TRUE;
                n++;
            }
            return FALSE;
        }
        char pc = (*p >= 'a' && *p <= 'z') ? *p - 32 : *p;
        char nc = (*n >= 'a' && *n <= 'z') ? *n - 32 : *n;
        if (pc == '?' || pc == nc) { p++; n++; }
        else return FALSE;
    }

    while (*p == '*') p++;
    return (*p == '\0' && *n == '\0');
}

/* ---- Hook implementations ---- */

static HANDLE WINAPI Hook_FindFirstFileW(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData) {
    if (lpFileName) {
        char narrowPath[260];
        VFS_WideToNarrow(lpFileName, narrowPath, 260);

        if (IsVirtualDirectory(narrowPath)) {
            VFS_CONTEXT* ctx = VFS_GetContext();
            if (ctx) {
                /* Split into directory and pattern */
                char dirPart[260] = {0};
                char patPart[260] = {0};
                strncpy_s(dirPart, 260, narrowPath, 259);
                char* lastSlash = strrchr(dirPart, '\\');
                if (lastSlash) {
                    strncpy_s(patPart, 260, lastSlash + 1, 259);
                    *lastSlash = '\0';
                } else {
                    strncpy_s(patPart, 260, dirPart, 259);
                    dirPart[0] = '\0';
                }
                VFS_NormalizePath(dirPart, 260);
                VFS_NormalizePath(patPart, 260);

                /* Collect matching files */
                uint32_t matches[1024];
                uint32_t matchCount = 0;

                for (uint32_t i = 0; i < ctx->header->file_count && matchCount < 1024; i++) {
                    VFS_FILE_ENTRY* file = &ctx->files[i];
                    const char* fileName = ctx->string_pool + file->name_offset;
                    if (WildcardMatch(patPart, fileName)) {
                        matches[matchCount++] = i;
                    }
                }

                if (matchCount > 0) {
                    HANDLE hFind = AllocFindHandle();
                    if (hFind != INVALID_HANDLE_VALUE) {
                        VFIND_HANDLE* fh = HookFind_GetHandle(hFind);
                        fh->matchIndices = (uint32_t*)malloc(matchCount * sizeof(uint32_t));
                        if (fh->matchIndices) {
                            memcpy(fh->matchIndices, matches, matchCount * sizeof(uint32_t));
                            fh->matchCount = matchCount;
                            fh->matchPos = 0;
                            strncpy_s(fh->searchDir, 260, dirPart, 259);
                            strncpy_s(fh->pattern, 260, patPart, 259);

                            /* Fill first result */
                            VFS_FILE_ENTRY* firstFile = &ctx->files[fh->matchIndices[0]];
                            const char* firstName = ctx->string_pool + firstFile->name_offset;
                            memset(lpFindFileData, 0, sizeof(WIN32_FIND_DATAW));
                            MultiByteToWideChar(CP_ACP, 0, firstName, -1,
                                lpFindFileData->cFileName, 260);
                            lpFindFileData->dwFileAttributes = firstFile->attributes;
                            lpFindFileData->nFileSizeLow = firstFile->original_size;
                            lpFindFileData->ftLastWriteTime.dwLowDateTime = (DWORD)firstFile->last_write_time;
                            lpFindFileData->ftLastWriteTime.dwHighDateTime = (DWORD)(firstFile->last_write_time >> 32);
                            fh->matchPos = 1;
                            return hFind;
                        }
                        HookFind_FreeHandle(hFind);
                    }
                }

                /* No matches found - return INVALID_HANDLE_VALUE with ERROR_FILE_NOT_FOUND */
                SetLastError(ERROR_FILE_NOT_FOUND);
                return INVALID_HANDLE_VALUE;
            }
        }
    }

    return g_orig_FindFirstFileW(lpFileName, lpFindFileData);
}

static BOOL WINAPI Hook_FindNextFileW(HANDLE hFindFile, LPWIN32_FIND_DATAW lpFindFileData) {
    VFIND_HANDLE* fh = HookFind_GetHandle(hFindFile);
    if (fh) {
        VFS_CONTEXT* ctx = VFS_GetContext();
        if (ctx && fh->matchPos < fh->matchCount) {
            VFS_FILE_ENTRY* file = &ctx->files[fh->matchIndices[fh->matchPos]];
            const char* fileName = ctx->string_pool + file->name_offset;
            memset(lpFindFileData, 0, sizeof(WIN32_FIND_DATAW));
            MultiByteToWideChar(CP_ACP, 0, fileName, -1,
                lpFindFileData->cFileName, 260);
            lpFindFileData->dwFileAttributes = file->attributes;
            lpFindFileData->nFileSizeLow = file->original_size;
            lpFindFileData->ftLastWriteTime.dwLowDateTime = (DWORD)file->last_write_time;
            lpFindFileData->ftLastWriteTime.dwHighDateTime = (DWORD)(file->last_write_time >> 32);
            fh->matchPos++;
            return TRUE;
        }
        SetLastError(ERROR_NO_MORE_FILES);
        return FALSE;
    }

    return g_orig_FindNextFileW(hFindFile, lpFindFileData);
}

static HANDLE WINAPI Hook_FindFirstFileA(LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData) {
    if (lpFileName && IsVirtualDirectory(lpFileName)) {
        /* Convert to wide and use W version */
        wchar_t widePath[260];
        MultiByteToWideChar(CP_ACP, 0, lpFileName, -1, widePath, 260);
        WIN32_FIND_DATAW findDataW;
        HANDLE hFind = Hook_FindFirstFileW(widePath, &findDataW);
        if (hFind != INVALID_HANDLE_VALUE) {
            memset(lpFindFileData, 0, sizeof(WIN32_FIND_DATAA));
            WideCharToMultiByte(CP_ACP, 0, findDataW.cFileName, -1,
                lpFindFileData->cFileName, 260, NULL, NULL);
            lpFindFileData->dwFileAttributes = findDataW.dwFileAttributes;
            lpFindFileData->nFileSizeLow = findDataW.nFileSizeLow;
            lpFindFileData->ftLastWriteTime = findDataW.ftLastWriteTime;
        }
        return hFind;
    }
    return g_orig_FindFirstFileA(lpFileName, lpFindFileData);
}

static BOOL WINAPI Hook_FindNextFileA(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData) {
    VFIND_HANDLE* fh = HookFind_GetHandle(hFindFile);
    if (fh) {
        WIN32_FIND_DATAW findDataW;
        if (Hook_FindNextFileW(hFindFile, &findDataW)) {
            memset(lpFindFileData, 0, sizeof(WIN32_FIND_DATAA));
            WideCharToMultiByte(CP_ACP, 0, findDataW.cFileName, -1,
                lpFindFileData->cFileName, 260, NULL, NULL);
            lpFindFileData->dwFileAttributes = findDataW.dwFileAttributes;
            lpFindFileData->nFileSizeLow = findDataW.nFileSizeLow;
            lpFindFileData->ftLastWriteTime = findDataW.ftLastWriteTime;
            return TRUE;
        }
        return FALSE;
    }
    return g_orig_FindNextFileA(hFindFile, lpFindFileData);
}

/* ---- Installation ---- */

int32_t HookFind_Install(void) {
    if (MH_CreateHook(&FindFirstFileW, &Hook_FindFirstFileW, (void**)&g_orig_FindFirstFileW) != MH_OK)
        return -1;
    if (MH_CreateHook(&FindFirstFileA, &Hook_FindFirstFileA, (void**)&g_orig_FindFirstFileA) != MH_OK)
        return -2;
    if (MH_CreateHook(&FindNextFileW, &Hook_FindNextFileW, (void**)&g_orig_FindNextFileW) != MH_OK)
        return -3;
    if (MH_CreateHook(&FindNextFileA, &Hook_FindNextFileA, (void**)&g_orig_FindNextFileA) != MH_OK)
        return -4;
    return 0;
}
