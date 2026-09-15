#ifndef VFS_TYPES_H
#define VFS_TYPES_H

#include <stdint.h>
#include <windows.h>

#define VFS_MAGIC 0x42494E45
#define VFS_VERSION 2
#define VFS_INVALID_INDEX 0xFFFFFFFF

#pragma pack(push, 1)
typedef struct _VFS_HEADER {
    uint32_t magic;
    uint32_t version;
    uint32_t file_count;
    uint32_t dir_count;
    uint32_t metadata_offset;
    uint32_t metadata_size;
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t loader_offset;
    uint32_t loader_size;
    uint32_t checksum;
    /* v2: 注册表虚拟化预置值区（blob 内偏移与大小；0/0 = 无）。写入端
     * VfsBuilder.SerializeRegistryRegion，读取端 VReg_Preload，格式契约两端同步。 */
    uint32_t registry_offset;
    uint32_t registry_size;
} VFS_HEADER;

typedef struct _VFS_DIR_ENTRY {
    uint32_t name_offset;
    uint32_t parent_index;
    uint32_t first_child;
    uint32_t next_sibling;
    uint32_t first_file;
} VFS_DIR_ENTRY;

typedef struct _VFS_FILE_ENTRY {
    uint32_t name_offset;
    uint32_t dir_index;
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t original_size;
    uint32_t attributes;
    uint64_t last_write_time;
    uint8_t  is_compressed;
    uint8_t  is_virtualized;
    uint8_t  reserved[2];
} VFS_FILE_ENTRY;
#pragma pack(pop)

typedef struct _VFS_HASH_ENTRY {
    uint32_t          path_hash;
    char              full_path[260];
    uint32_t          file_index;
    struct _VFS_HASH_ENTRY* next;
} VFS_HASH_ENTRY;

typedef struct _VFS_CONTEXT {
    VFS_HEADER*       header;
    VFS_DIR_ENTRY*    dirs;
    VFS_FILE_ENTRY*   files;
    const char*       string_pool;
    uint8_t*          data_region;
    VFS_HASH_ENTRY**  hash_table;
    uint32_t          hash_buckets;
    CRITICAL_SECTION  lock;
    BOOL              initialized;
} VFS_CONTEXT;

typedef struct _VFS_FILE_HANDLE {
    uint32_t      file_index;
    uint64_t      current_pos;
    uint8_t*      decompressed;
    uint32_t      decompressed_size;
    DWORD         access_mode;
    DWORD         share_mode;
    BOOL          is_virtual;
} VFS_FILE_HANDLE;

typedef struct _VFS_FIND_HANDLE {
    char          search_path[260];
    uint32_t      current_index;
    uint32_t*     match_indices;
    uint32_t      match_count;
    uint32_t      match_position;
    BOOL          is_virtual;
} VFS_FIND_HANDLE;

typedef struct _VFS_MAPPING_HANDLE {
    uint32_t      file_index;
    uint8_t*      mapped_data;
    uint32_t      mapped_size;
    BOOL          is_virtual;
} VFS_MAPPING_HANDLE;

#endif
