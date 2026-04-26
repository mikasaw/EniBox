#ifndef VFS_HASHTABLE_H
#define VFS_HASHTABLE_H
#include "vfs_types.h"
uint32_t VFS_Fnv1aHash(const char* data, uint32_t length);
int32_t VFS_HashTableBuild(VFS_CONTEXT* ctx);
VFS_HASH_ENTRY* VFS_HashTableLookup(VFS_CONTEXT* ctx, const char* path);
void VFS_HashTableFree(VFS_CONTEXT* ctx);
#endif
