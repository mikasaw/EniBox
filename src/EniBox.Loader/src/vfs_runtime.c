#include "vfs_runtime.h"
#include "vfs_hashtable.h"
#include "vfs_handle.h"
#include "lzma_dec.h"
#include "crc32.h"
#include <stdlib.h>
#include <string.h>

static VFS_CONTEXT g_vfs_ctx = {0};

int32_t VFS_Initialize(uint8_t* section_base, uint32_t section_size) {
    if (!section_base || section_size < sizeof(VFS_HEADER)) return -1;
    VFS_HEADER* header = (VFS_HEADER*)section_base;
    if (header->magic != VFS_MAGIC) return -2;
    if (header->version < VFS_VERSION) return -3;

    uint32_t saved_checksum = header->checksum;
    header->checksum = 0;
    uint32_t computed = CRC32_Compute(section_base, section_size);
    header->checksum = saved_checksum;
    if (computed != saved_checksum) return -4;

    g_vfs_ctx.header = header;
    g_vfs_ctx.data_region = section_base + header->data_offset;
    uint8_t* meta_base = section_base + header->metadata_offset;
    g_vfs_ctx.dirs = (VFS_DIR_ENTRY*)(meta_base + sizeof(VFS_HEADER));
    g_vfs_ctx.files = (VFS_FILE_ENTRY*)(meta_base + sizeof(VFS_HEADER) + header->dir_count * sizeof(VFS_DIR_ENTRY));
    g_vfs_ctx.string_pool = (const char*)(meta_base + sizeof(VFS_HEADER) + header->dir_count * sizeof(VFS_DIR_ENTRY) + header->file_count * sizeof(VFS_FILE_ENTRY));

    InitializeCriticalSection(&g_vfs_ctx.lock);
    int32_t result = VFS_HashTableBuild(&g_vfs_ctx);
    if (result != 0) { DeleteCriticalSection(&g_vfs_ctx.lock); return -5; }
    result = VFS_HandleTableInit();
    if (result != 0) { VFS_HashTableFree(&g_vfs_ctx); DeleteCriticalSection(&g_vfs_ctx.lock); return -6; }
    g_vfs_ctx.initialized = TRUE;
    return 0;
}

void VFS_Finalize(void) {
    if (!g_vfs_ctx.initialized) return;
    VFS_HandleTableCleanup();
    VFS_HashTableFree(&g_vfs_ctx);
    DeleteCriticalSection(&g_vfs_ctx.lock);
    g_vfs_ctx.initialized = FALSE;
    g_vfs_ctx.header = NULL;
}

VFS_CONTEXT* VFS_GetContext(void) { return g_vfs_ctx.initialized ? &g_vfs_ctx : NULL; }

VFS_FILE_ENTRY* VFS_LookupFile(const char* path) {
    VFS_CONTEXT* ctx = VFS_GetContext();
    if (!ctx || !path) return NULL;
    VFS_HASH_ENTRY* entry = VFS_HashTableLookup(ctx, path);
    if (entry && entry->file_index < ctx->header->file_count) return &ctx->files[entry->file_index];
    return NULL;
}

VFS_FILE_ENTRY* VFS_LookupFileW(const wchar_t* path) {
    if (!path) return NULL;
    char narrow[260];
    VFS_WideToNarrow(path, narrow, 260);
    return VFS_LookupFile(narrow);
}

int32_t VFS_ReadFile(VFS_FILE_HANDLE* handle, void* buffer, uint32_t bytes_to_read, uint32_t* bytes_read) {
    if (!handle || !buffer || !bytes_read) return -1;
    VFS_CONTEXT* ctx = VFS_GetContext();
    if (!ctx) return -2;
    VFS_FILE_ENTRY* file = &ctx->files[handle->file_index];
    if (file->is_compressed && !handle->decompressed) {
        int32_t result = VFS_DecompressFile(handle);
        if (result != 0) return result;
    }
    const uint8_t* data;
    uint32_t data_size;
    if (file->is_compressed && handle->decompressed) {
        data = handle->decompressed; data_size = handle->decompressed_size;
    } else {
        data = ctx->data_region + file->data_offset; data_size = file->original_size;
    }
    if (handle->current_pos >= data_size) { *bytes_read = 0; return 0; }
    uint32_t available = data_size - (uint32_t)handle->current_pos;
    uint32_t to_read = bytes_to_read < available ? bytes_to_read : available;
    memcpy(buffer, data + handle->current_pos, to_read);
    handle->current_pos += to_read;
    *bytes_read = to_read;
    return 0;
}

uint32_t VFS_GetFileSize(VFS_FILE_HANDLE* handle) {
    if (!handle) return 0;
    VFS_CONTEXT* ctx = VFS_GetContext();
    if (!ctx || handle->file_index >= ctx->header->file_count) return 0;
    return ctx->files[handle->file_index].original_size;
}

int32_t VFS_DecompressFile(VFS_FILE_HANDLE* handle) {
    if (!handle) return -1;
    VFS_CONTEXT* ctx = VFS_GetContext();
    if (!ctx) return -2;
    VFS_FILE_ENTRY* file = &ctx->files[handle->file_index];
    if (!file->is_compressed) return 0;
    if (handle->decompressed) return 0;
    handle->decompressed = (uint8_t*)VirtualAlloc(NULL, file->original_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!handle->decompressed) return -3;
    handle->decompressed_size = file->original_size;
    uint32_t output_size = file->original_size;
    int32_t result = LzmaDec_Decompress(ctx->data_region + file->data_offset, file->data_size, handle->decompressed, &output_size);
    if (result != 0) { VirtualFree(handle->decompressed, 0, MEM_RELEASE); handle->decompressed = NULL; handle->decompressed_size = 0; return -4; }
    return 0;
}

void VFS_NormalizePath(char* path, uint32_t size) {
    if (!path) return;
    uint32_t len = (uint32_t)strlen(path);
    for (uint32_t i = 0; i < len; i++) {
        if (path[i] == '/') path[i] = '\\';
        if (path[i] >= 'a' && path[i] <= 'z') path[i] = (char)(path[i] - 32);
    }
    if (len > 0 && path[0] == '\\') { memmove(path, path + 1, len); path[len - 1] = '\0'; }
}

void VFS_NormalizePathW(wchar_t* path, uint32_t size) {
    if (!path) return;
    uint32_t len = (uint32_t)wcslen(path);
    for (uint32_t i = 0; i < len; i++) {
        if (path[i] == L'/') path[i] = L'\\';
        if (path[i] >= L'a' && path[i] <= L'z') path[i] = (wchar_t)(path[i] - 32);
    }
    if (len > 0 && path[0] == L'\\') { memmove(path, path + 1, len * sizeof(wchar_t)); path[len - 1] = L'\0'; }
}

void VFS_WideToNarrow(const wchar_t* wide, char* narrow, uint32_t size) {
    if (!wide || !narrow || size == 0) return;
    WideCharToMultiByte(CP_ACP, 0, wide, -1, narrow, size, NULL, NULL);
}
