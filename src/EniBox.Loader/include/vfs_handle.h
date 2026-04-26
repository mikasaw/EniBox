#ifndef VFS_HANDLE_H
#define VFS_HANDLE_H
#include "vfs_types.h"
int32_t VFS_HandleTableInit(void);
VFS_FILE_HANDLE* VFS_HandleAlloc(uint32_t file_index, DWORD access_mode, DWORD share_mode);
VFS_FILE_HANDLE* VFS_HandleFromOsHandle(HANDLE hFile);
void VFS_HandleFree(VFS_FILE_HANDLE* handle);
HANDLE VFS_HandleToOsHandle(VFS_FILE_HANDLE* handle);
BOOL VFS_IsVirtualHandle(HANDLE hFile);
void VFS_HandleTableCleanup(void);
#endif
