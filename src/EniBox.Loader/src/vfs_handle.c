#include "vfs_handle.h"
#include <stdlib.h>
#include <string.h>

#define MAX_VIRTUAL_HANDLES 4096
#define VIRTUAL_HANDLE_BASE 0xFFFF0000

static VFS_FILE_HANDLE g_handle_table[MAX_VIRTUAL_HANDLES];
static BOOL g_handle_used[MAX_VIRTUAL_HANDLES];
static uint32_t g_handle_count = 0;

int32_t VFS_HandleTableInit(void) {
    memset(g_handle_table, 0, sizeof(g_handle_table));
    memset(g_handle_used, 0, sizeof(g_handle_used));
    g_handle_count = 0;
    return 0;
}

VFS_FILE_HANDLE* VFS_HandleAlloc(uint32_t file_index, DWORD access_mode, DWORD share_mode) {
    for (uint32_t i = 0; i < MAX_VIRTUAL_HANDLES; i++) {
        if (!g_handle_used[i]) {
            g_handle_used[i] = TRUE;
            g_handle_table[i].file_index = file_index;
            g_handle_table[i].current_pos = 0;
            g_handle_table[i].decompressed = NULL;
            g_handle_table[i].decompressed_size = 0;
            g_handle_table[i].access_mode = access_mode;
            g_handle_table[i].share_mode = share_mode;
            g_handle_table[i].is_virtual = TRUE;
            g_handle_count++;
            return &g_handle_table[i];
        }
    }
    return NULL;
}

VFS_FILE_HANDLE* VFS_HandleFromOsHandle(HANDLE hFile) {
    if (!VFS_IsVirtualHandle(hFile)) return NULL;
    uint32_t index = (uint32_t)((uintptr_t)hFile - VIRTUAL_HANDLE_BASE);
    if (index >= MAX_VIRTUAL_HANDLES || !g_handle_used[index]) return NULL;
    return &g_handle_table[index];
}

void VFS_HandleFree(VFS_FILE_HANDLE* handle) {
    if (!handle) return;
    uint32_t index = (uint32_t)(handle - g_handle_table);
    if (index >= MAX_VIRTUAL_HANDLES) return;
    if (handle->decompressed) {
        VirtualFree(handle->decompressed, 0, MEM_RELEASE);
        handle->decompressed = NULL;
    }
    g_handle_used[index] = FALSE;
    g_handle_count--;
}

HANDLE VFS_HandleToOsHandle(VFS_FILE_HANDLE* handle) {
    if (!handle) return INVALID_HANDLE_VALUE;
    uint32_t index = (uint32_t)(handle - g_handle_table);
    if (index >= MAX_VIRTUAL_HANDLES) return INVALID_HANDLE_VALUE;
    return (HANDLE)(uintptr_t)(VIRTUAL_HANDLE_BASE + index);
}

BOOL VFS_IsVirtualHandle(HANDLE hFile) {
    uintptr_t val = (uintptr_t)hFile;
    return (val >= VIRTUAL_HANDLE_BASE && val < VIRTUAL_HANDLE_BASE + MAX_VIRTUAL_HANDLES);
}

void VFS_HandleTableCleanup(void) {
    for (uint32_t i = 0; i < MAX_VIRTUAL_HANDLES; i++) {
        if (g_handle_used[i]) VFS_HandleFree(&g_handle_table[i]);
    }
    g_handle_count = 0;
}
