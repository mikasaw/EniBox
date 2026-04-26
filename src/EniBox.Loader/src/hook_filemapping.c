/*
 * FileMapping Hooks - CreateFileMappingA/W, MapViewOfFile
 * 
 * These hooks support memory-mapped file access to virtual files.
 * When an application maps a virtual file into memory, we provide
 * a memory region containing the file's decompressed content.
 */

#include "hook_filemapping.h"
#include "hook_findfile.h"
#include "vfs_runtime.h"
#include "vfs_handle.h"
#include "../deps/MinHook/include/MinHook.h"
#include <stdlib.h>
#include <string.h>

/* ---- Original function pointers ---- */

typedef HANDLE  (WINAPI *CreateFileMappingW_t)(HANDLE, LPSECURITY_ATTRIBUTES, DWORD, DWORD, DWORD, LPCWSTR);
typedef HANDLE  (WINAPI *CreateFileMappingA_t)(HANDLE, LPSECURITY_ATTRIBUTES, DWORD, DWORD, DWORD, LPCSTR);
typedef LPVOID  (WINAPI *MapViewOfFile_t)(HANDLE, DWORD, DWORD, DWORD, SIZE_T);
typedef BOOL    (WINAPI *UnmapViewOfFile_t)(LPCVOID);
typedef BOOL    (WINAPI *CloseHandle_t)(HANDLE);

static CreateFileMappingW_t g_orig_CreateFileMappingW = NULL;
static CreateFileMappingA_t g_orig_CreateFileMappingA = NULL;
static MapViewOfFile_t      g_orig_MapViewOfFile      = NULL;
static UnmapViewOfFile_t    g_orig_UnmapViewOfFile    = NULL;
static CloseHandle_t        g_orig_CloseHandle        = NULL;

/* ---- Virtual mapping handle management ---- */

#define MAX_MAPPING_HANDLES 256
#define MAPPING_HANDLE_BASE 0xDFFF0000

typedef struct _VMAP_HANDLE {
    BOOL          inUse;
    uint32_t      file_index;     /* VFS file index */
    uint8_t*      mapped_data;    /* Decompressed file data */
    uint32_t      mapped_size;    /* Size of mapped data */
    DWORD         protect;        /* Page protection */
} VMAP_HANDLE;

static VMAP_HANDLE g_mapHandles[MAX_MAPPING_HANDLES];

static VMAP_HANDLE* GetMapHandle(HANDLE h) {
    uintptr_t val = (uintptr_t)h;
    if (val < MAPPING_HANDLE_BASE || val >= MAPPING_HANDLE_BASE + MAX_MAPPING_HANDLES)
        return NULL;
    uint32_t idx = (uint32_t)(val - MAPPING_HANDLE_BASE);
    if (!g_mapHandles[idx].inUse) return NULL;
    return &g_mapHandles[idx];
}

static HANDLE AllocMapHandle(void) {
    for (uint32_t i = 0; i < MAX_MAPPING_HANDLES; i++) {
        if (!g_mapHandles[i].inUse) {
            g_mapHandles[i].inUse = TRUE;
            return (HANDLE)(uintptr_t)(MAPPING_HANDLE_BASE + i);
        }
    }
    return NULL;
}

static void FreeMapHandle(HANDLE h) {
    VMAP_HANDLE* mh = GetMapHandle(h);
    if (mh) {
        mh->inUse = FALSE;
        /* Don't free mapped_data here - it may still be in use via MapViewOfFile */
    }
}

/* ---- Hook implementations ---- */

static HANDLE WINAPI Hook_CreateFileMappingW(
    HANDLE hFile, LPSECURITY_ATTRIBUTES lpAttributes, DWORD flProtect,
    DWORD dwMaximumSizeHigh, DWORD dwMaximumSizeLow, LPCWSTR lpName)
{
    if (VFS_IsVirtualHandle(hFile) && lpName == NULL) {
        /* Named mappings are not supported for virtual files */
        VFS_FILE_HANDLE* vfh = VFS_HandleFromOsHandle(hFile);
        if (vfh) {
            VFS_CONTEXT* ctx = VFS_GetContext();
            if (ctx) {
                /* Ensure file data is decompressed */
                if (ctx->files[vfh->file_index].is_compressed && !vfh->decompressed) {
                    VFS_DecompressFile(vfh);
                }

                HANDLE hMap = AllocMapHandle();
                if (hMap) {
                    VMAP_HANDLE* mh = GetMapHandle(hMap);
                    mh->file_index = vfh->file_index;
                    mh->mapped_data = vfh->decompressed ? vfh->decompressed :
                        (ctx->data_region + ctx->files[vfh->file_index].data_offset);
                    mh->mapped_size = ctx->files[vfh->file_index].original_size;
                    mh->protect = flProtect;
                    return hMap;
                }
            }
        }
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }

    return g_orig_CreateFileMappingW(hFile, lpAttributes, flProtect,
        dwMaximumSizeHigh, dwMaximumSizeLow, lpName);
}

static HANDLE WINAPI Hook_CreateFileMappingA(
    HANDLE hFile, LPSECURITY_ATTRIBUTES lpAttributes, DWORD flProtect,
    DWORD dwMaximumSizeHigh, DWORD dwMaximumSizeLow, LPCSTR lpName)
{
    if (VFS_IsVirtualHandle(hFile) && lpName == NULL) {
        /* Same logic as W version */
        return Hook_CreateFileMappingW(hFile, lpAttributes, flProtect,
            dwMaximumSizeHigh, dwMaximumSizeLow, NULL);
    }

    return g_orig_CreateFileMappingA(hFile, lpAttributes, flProtect,
        dwMaximumSizeHigh, dwMaximumSizeLow, lpName);
}

static LPVOID WINAPI Hook_MapViewOfFile(
    HANDLE hFileMappingObject, DWORD dwDesiredAccess,
    DWORD dwFileOffsetHigh, DWORD dwFileOffsetLow, SIZE_T dwNumberOfBytesToMap)
{
    VMAP_HANDLE* mh = GetMapHandle(hFileMappingObject);
    if (mh) {
        uint32_t offset = dwFileOffsetLow;
        uint32_t size = dwNumberOfBytesToMap ? (uint32_t)dwNumberOfBytesToMap : (mh->mapped_size - offset);

        if (offset >= mh->mapped_size) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return NULL;
        }

        if (offset + size > mh->mapped_size)
            size = mh->mapped_size - offset;

        /* Return pointer into the decompressed data buffer */
        return (LPVOID)(mh->mapped_data + offset);
    }

    return g_orig_MapViewOfFile(hFileMappingObject, dwDesiredAccess,
        dwFileOffsetHigh, dwFileOffsetLow, dwNumberOfBytesToMap);
}

static BOOL WINAPI Hook_UnmapViewOfFile(LPCVOID lpBaseAddress) {
    /* For virtual mappings, the data is part of the VFS handle's decompressed buffer.
     * We don't need to do anything special for UnmapViewOfFile since the memory
     * is managed by the VFS handle. Just return TRUE. */
    for (uint32_t i = 0; i < MAX_MAPPING_HANDLES; i++) {
        if (g_mapHandles[i].inUse) {
            VFS_CONTEXT* ctx = VFS_GetContext();
            if (ctx) {
                VFS_FILE_HANDLE* vfh = VFS_HandleFromOsHandle(
                    (HANDLE)(uintptr_t)(0xFFFF0000 + g_mapHandles[i].file_index));
                if (vfh && vfh->decompressed) {
                    if ((uintptr_t)lpBaseAddress >= (uintptr_t)vfh->decompressed &&
                        (uintptr_t)lpBaseAddress < (uintptr_t)(vfh->decompressed + vfh->decompressed_size)) {
                        return TRUE; /* Virtual mapping - no actual unmap needed */
                    }
                }
            }
        }
    }

    return g_orig_UnmapViewOfFile(lpBaseAddress);
}

static BOOL WINAPI Hook_CloseHandle(HANDLE hObject) {
    /* Check if this is a virtual find handle */
    VFIND_HANDLE* fh = HookFind_GetHandle(hObject);
    if (fh) {
        HookFind_FreeHandle(hObject);
        return TRUE;
    }

    /* Check if this is a virtual mapping handle */
    VMAP_HANDLE* mh = GetMapHandle(hObject);
    if (mh) {
        FreeMapHandle(hObject);
        return TRUE;
    }

    /* Check if this is a virtual file handle */
    if (VFS_IsVirtualHandle(hObject)) {
        VFS_FILE_HANDLE* vfh = VFS_HandleFromOsHandle(hObject);
        if (vfh) {
            VFS_HandleFree(vfh);
            return TRUE;
        }
    }

    return g_orig_CloseHandle(hObject);
}

/* ---- Installation ---- */

int32_t HookMapping_Install(void) {
    if (MH_CreateHook(&CreateFileMappingW, &Hook_CreateFileMappingW,
        (void**)&g_orig_CreateFileMappingW) != MH_OK) return -1;
    if (MH_CreateHook(&CreateFileMappingA, &Hook_CreateFileMappingA,
        (void**)&g_orig_CreateFileMappingA) != MH_OK) return -2;
    if (MH_CreateHook(&MapViewOfFile, &Hook_MapViewOfFile,
        (void**)&g_orig_MapViewOfFile) != MH_OK) return -3;
    if (MH_CreateHook(&UnmapViewOfFile, &Hook_UnmapViewOfFile,
        (void**)&g_orig_UnmapViewOfFile) != MH_OK) return -4;
    if (MH_CreateHook(&CloseHandle, &Hook_CloseHandle,
        (void**)&g_orig_CloseHandle) != MH_OK) return -5;
    return 0;
}
