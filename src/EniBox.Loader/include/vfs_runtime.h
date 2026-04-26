#ifndef VFS_RUNTIME_H
#define VFS_RUNTIME_H
#include "vfs_types.h"
int32_t VFS_Initialize(uint8_t* section_base, uint32_t section_size);
void VFS_Finalize(void);
VFS_CONTEXT* VFS_GetContext(void);
VFS_FILE_ENTRY* VFS_LookupFile(const char* path);
VFS_FILE_ENTRY* VFS_LookupFileW(const wchar_t* path);
int32_t VFS_ReadFile(VFS_FILE_HANDLE* handle, void* buffer, uint32_t bytes_to_read, uint32_t* bytes_read);
uint32_t VFS_GetFileSize(VFS_FILE_HANDLE* handle);
int32_t VFS_DecompressFile(VFS_FILE_HANDLE* handle);
void VFS_NormalizePath(char* path, uint32_t size);
void VFS_NormalizePathW(wchar_t* path, uint32_t size);
void VFS_WideToNarrow(const wchar_t* wide, char* narrow, uint32_t size);
#endif
