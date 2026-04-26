#include "vfs_hashtable.h"
#include "vfs_runtime.h"
#include <stdlib.h>
#include <string.h>

uint32_t VFS_Fnv1aHash(const char* data, uint32_t length) {
    uint32_t hash = 0x811C9DC5;
    for (uint32_t i = 0; i < length; i++) {
        hash ^= (uint8_t)data[i];
        hash *= 0x01000193;
    }
    return hash;
}

int32_t VFS_HashTableBuild(VFS_CONTEXT* ctx) {
    if (!ctx || !ctx->header || !ctx->files) return -1;
    uint32_t min_buckets = ctx->header->file_count * 2;
    uint32_t buckets = 1;
    while (buckets < min_buckets) buckets <<= 1;
    ctx->hash_buckets = buckets;
    ctx->hash_table = (VFS_HASH_ENTRY**)calloc(buckets, sizeof(VFS_HASH_ENTRY*));
    if (!ctx->hash_table) return -2;

    for (uint32_t i = 0; i < ctx->header->file_count; i++) {
        VFS_FILE_ENTRY* file = &ctx->files[i];
        VFS_HASH_ENTRY* entry = (VFS_HASH_ENTRY*)calloc(1, sizeof(VFS_HASH_ENTRY));
        if (!entry) return -3;

        const char* file_name = ctx->string_pool + file->name_offset;
        char full_path[260] = {0};
        char components[32][260];
        int comp_count = 0;
        strncpy_s(components[comp_count], 260, file_name, 259);
        comp_count++;
        uint32_t dir_idx = file->dir_index;
        while (dir_idx != VFS_INVALID_INDEX && dir_idx < ctx->header->dir_count) {
            VFS_DIR_ENTRY* dir = &ctx->dirs[dir_idx];
            const char* dir_name = ctx->string_pool + dir->name_offset;
            strncpy_s(components[comp_count], 260, dir_name, 259);
            comp_count++;
            dir_idx = dir->parent_index;
        }
        for (int c = comp_count - 1; c >= 0; c--) {
            if (c < comp_count - 1) strcat_s(full_path, 260, "\\");
            strcat_s(full_path, 260, components[c]);
        }
        VFS_NormalizePath(full_path, 260);
        strncpy_s(entry->full_path, 260, full_path, 259);
        entry->file_index = i;
        entry->path_hash = VFS_Fnv1aHash(full_path, (uint32_t)strlen(full_path));
        uint32_t bucket = entry->path_hash & (buckets - 1);
        entry->next = ctx->hash_table[bucket];
        ctx->hash_table[bucket] = entry;
    }
    return 0;
}

VFS_HASH_ENTRY* VFS_HashTableLookup(VFS_CONTEXT* ctx, const char* path) {
    if (!ctx || !ctx->hash_table || !path) return NULL;
    char normalized[260];
    strncpy_s(normalized, 260, path, 259);
    VFS_NormalizePath(normalized, 260);
    uint32_t hash = VFS_Fnv1aHash(normalized, (uint32_t)strlen(normalized));
    uint32_t bucket = hash & (ctx->hash_buckets - 1);
    EnterCriticalSection(&ctx->lock);
    VFS_HASH_ENTRY* entry = ctx->hash_table[bucket];
    while (entry) {
        if (entry->path_hash == hash && strcmp(entry->full_path, normalized) == 0) {
            LeaveCriticalSection(&ctx->lock);
            return entry;
        }
        entry = entry->next;
    }
    LeaveCriticalSection(&ctx->lock);
    return NULL;
}

void VFS_HashTableFree(VFS_CONTEXT* ctx) {
    if (!ctx || !ctx->hash_table) return;
    for (uint32_t i = 0; i < ctx->hash_buckets; i++) {
        VFS_HASH_ENTRY* entry = ctx->hash_table[i];
        while (entry) {
            VFS_HASH_ENTRY* next = entry->next;
            free(entry);
            entry = next;
        }
    }
    free(ctx->hash_table);
    ctx->hash_table = NULL;
    ctx->hash_buckets = 0;
}
