/*
 * NT API Hooks - NtCreateFile, NtOpenFile, NtReadFile
 * 
 * These hook the NT-level file API in ntdll.dll. Many applications
 * call these directly (especially those using the Win32 API internally,
 * as CreateFileW calls NtCreateFile).
 */

#include "hook_ntapi.h"
#include "vfs_runtime.h"
#include "vfs_handle.h"
#include "../deps/MinHook/include/MinHook.h"
#include <windows.h>

/* ---- NT types - self-contained definitions ---- */
/* We define all needed NT types ourselves because winternl.h is incomplete
   under /permissive- mode and doesn't provide PUNICODE_STRING etc. */

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((LONG)0x00000000)
#endif

typedef LONG NTSTATUS_NT;

typedef struct _UNICODE_STRING_NT {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
} UNICODE_STRING_NT;
typedef UNICODE_STRING_NT *PUNICODE_STRING_NT;

typedef struct _OBJECT_ATTRIBUTES_NT {
    ULONG           Length;
    HANDLE          RootDirectory;
    PUNICODE_STRING_NT ObjectName;
    PVOID           SecurityDescriptor;
    PVOID           SecurityQualityOfService;
} OBJECT_ATTRIBUTES_NT;
typedef OBJECT_ATTRIBUTES_NT *POBJECT_ATTRIBUTES_NT;

typedef struct _IO_STATUS_BLOCK_NT {
    NTSTATUS_NT Status;
    ULONG_PTR Information;
} IO_STATUS_BLOCK_NT;
typedef IO_STATUS_BLOCK_NT *PIO_STATUS_BLOCK_NT;

typedef VOID (NTAPI *PIO_APC_ROUTINE_NT)(
    PVOID ApcContext,
    PIO_STATUS_BLOCK_NT IoStatusBlock,
    ULONG Reserved);

/* ---- NT function typedefs ---- */

typedef NTSTATUS_NT (NTAPI *NtCreateFile_t)(
    PHANDLE FileHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES_NT ObjectAttributes,
    PIO_STATUS_BLOCK_NT IoStatusBlock,
    PLARGE_INTEGER AllocationSize,
    ULONG FileAttributes,
    ULONG ShareAccess,
    ULONG CreateDisposition,
    ULONG CreateOptions,
    PVOID EaBuffer,
    ULONG EaLength);

typedef NTSTATUS_NT (NTAPI *NtOpenFile_t)(
    PHANDLE FileHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES_NT ObjectAttributes,
    PIO_STATUS_BLOCK_NT IoStatusBlock,
    ULONG ShareAccess,
    ULONG OpenOptions);

typedef NTSTATUS_NT (NTAPI *NtReadFile_t)(
    HANDLE FileHandle,
    HANDLE Event,
    PIO_APC_ROUTINE_NT ApcRoutine,
    PVOID ApcContext,
    PIO_STATUS_BLOCK_NT IoStatusBlock,
    PVOID Buffer,
    ULONG Length,
    PLARGE_INTEGER ByteOffset,
    PULONG Key);

/* ---- Original function pointers ---- */

static NtCreateFile_t g_orig_NtCreateFile = NULL;
static NtOpenFile_t   g_orig_NtOpenFile   = NULL;
static NtReadFile_t   g_orig_NtReadFile   = NULL;

/* ---- Helper: Extract Win32 path from OBJECT_ATTRIBUTES_NT ---- */

static BOOL NtPathToWin32Path(POBJECT_ATTRIBUTES_NT objAttrs, wchar_t* win32Path, uint32_t pathSize) {
    if (!objAttrs || !objAttrs->ObjectName || !win32Path)
        return FALSE;

    UNICODE_STRING_NT* name = objAttrs->ObjectName;
    if (name->Length == 0 || name->Buffer == NULL)
        return FALSE;

    /* NT paths typically start with \??\ or \DosDevices\ */
    const wchar_t* ntPath = name->Buffer;
    uint32_t ntLen = name->Length / sizeof(wchar_t);

    if (ntLen >= 4 && ntPath[0] == L'\\' && ntPath[1] == L'?' &&
        ntPath[2] == L'?' && ntPath[3] == L'\\') {
        /* \??\C:\path -> C:\path */
        if (ntLen - 4 >= pathSize)
            return FALSE;
        wcsncpy_s(win32Path, pathSize, ntPath + 4, ntLen - 4);
        win32Path[ntLen - 4] = L'\0';
        return TRUE;
    }

    if (ntLen >= 12 && wcsncmp(ntPath, L"\\DosDevices\\", 12) == 0) {
        /* \DosDevices\C:\path -> C:\path */
        if (ntLen - 12 >= pathSize)
            return FALSE;
        wcsncpy_s(win32Path, pathSize, ntPath + 12, ntLen - 12);
        win32Path[ntLen - 12] = L'\0';
        return TRUE;
    }

    /* Fallback: copy as-is */
    if (ntLen >= pathSize)
        return FALSE;
    wcsncpy_s(win32Path, pathSize, ntPath, ntLen);
    win32Path[ntLen] = L'\0';
    return TRUE;
}

/* ---- Hook implementations ---- */

static NTSTATUS_NT NTAPI Hook_NtCreateFile(
    PHANDLE FileHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES_NT ObjectAttributes,
    PIO_STATUS_BLOCK_NT IoStatusBlock,
    PLARGE_INTEGER AllocationSize,
    ULONG FileAttributes,
    ULONG ShareAccess,
    ULONG CreateDisposition,
    ULONG CreateOptions,
    PVOID EaBuffer,
    ULONG EaLength)
{
    if (ObjectAttributes) {
        wchar_t win32Path[260];
        if (NtPathToWin32Path(ObjectAttributes, win32Path, 260)) {
            VFS_FILE_ENTRY* file = VFS_LookupFileW(win32Path);
            if (file && file->is_virtualized) {
                VFS_CONTEXT* ctx = VFS_GetContext();
                if (ctx) {
                    uint32_t idx = (uint32_t)(file - ctx->files);
                    DWORD accessMode = 0;
                    if (DesiredAccess & GENERIC_READ) accessMode |= GENERIC_READ;
                    if (DesiredAccess & GENERIC_WRITE) accessMode |= GENERIC_WRITE;
                    VFS_FILE_HANDLE* handle = VFS_HandleAlloc(idx, accessMode, ShareAccess);
                    if (handle) {
                        *FileHandle = VFS_HandleToOsHandle(handle);
                        if (IoStatusBlock)
                            IoStatusBlock->Status = 0;
                        return 0; /* STATUS_SUCCESS */
                    }
                }
            }
        }
    }

    return g_orig_NtCreateFile(FileHandle, DesiredAccess, ObjectAttributes,
        IoStatusBlock, AllocationSize, FileAttributes, ShareAccess,
        CreateDisposition, CreateOptions, EaBuffer, EaLength);
}

static NTSTATUS_NT NTAPI Hook_NtOpenFile(
    PHANDLE FileHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES_NT ObjectAttributes,
    PIO_STATUS_BLOCK_NT IoStatusBlock,
    ULONG ShareAccess,
    ULONG OpenOptions)
{
    if (ObjectAttributes) {
        wchar_t win32Path[260];
        if (NtPathToWin32Path(ObjectAttributes, win32Path, 260)) {
            VFS_FILE_ENTRY* file = VFS_LookupFileW(win32Path);
            if (file && file->is_virtualized) {
                VFS_CONTEXT* ctx = VFS_GetContext();
                if (ctx) {
                    uint32_t idx = (uint32_t)(file - ctx->files);
                    VFS_FILE_HANDLE* handle = VFS_HandleAlloc(idx, GENERIC_READ, ShareAccess);
                    if (handle) {
                        *FileHandle = VFS_HandleToOsHandle(handle);
                        if (IoStatusBlock)
                            IoStatusBlock->Status = 0;
                        return 0;
                    }
                }
            }
        }
    }

    return g_orig_NtOpenFile(FileHandle, DesiredAccess, ObjectAttributes,
        IoStatusBlock, ShareAccess, OpenOptions);
}

static NTSTATUS_NT NTAPI Hook_NtReadFile(
    HANDLE FileHandle,
    HANDLE Event,
    PIO_APC_ROUTINE_NT ApcRoutine,
    PVOID ApcContext,
    PIO_STATUS_BLOCK_NT IoStatusBlock,
    PVOID Buffer,
    ULONG Length,
    PLARGE_INTEGER ByteOffset,
    PULONG Key)
{
    if (VFS_IsVirtualHandle(FileHandle)) {
        VFS_FILE_HANDLE* handle = VFS_HandleFromOsHandle(FileHandle);
        if (handle) {
            /* Handle seek position from ByteOffset */
            if (ByteOffset) {
                handle->current_pos = (uint64_t)ByteOffset->QuadPart;
            }

            uint32_t bytes_read = 0;
            int32_t result = VFS_ReadFile(handle, Buffer, Length, &bytes_read);
            if (IoStatusBlock) {
                IoStatusBlock->Status = (result == 0) ? 0 : 0xC0000004; /* STATUS_IO_DEVICE_ERROR */
                IoStatusBlock->Information = bytes_read;
            }
            return (result == 0) ? 0 : 0xC0000004;
        }
    }

    return g_orig_NtReadFile(FileHandle, Event, ApcRoutine, ApcContext,
        IoStatusBlock, Buffer, Length, ByteOffset, Key);
}

/* ---- Installation ---- */

int32_t HookNt_Install(void) {
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtdll) return 0; /* ntdll always loaded, but be safe */

    void* pNtCreateFile = GetProcAddress(hNtdll, "NtCreateFile");
    void* pNtOpenFile   = GetProcAddress(hNtdll, "NtOpenFile");
    void* pNtReadFile   = GetProcAddress(hNtdll, "NtReadFile");

    if (pNtCreateFile) {
        if (MH_CreateHook(pNtCreateFile, &Hook_NtCreateFile, (void**)&g_orig_NtCreateFile) != MH_OK)
            return -1;
    }

    if (pNtOpenFile) {
        if (MH_CreateHook(pNtOpenFile, &Hook_NtOpenFile, (void**)&g_orig_NtOpenFile) != MH_OK)
            return -2;
    }

    if (pNtReadFile) {
        if (MH_CreateHook(pNtReadFile, &Hook_NtReadFile, (void**)&g_orig_NtReadFile) != MH_OK)
            return -3;
    }

    return 0;
}
