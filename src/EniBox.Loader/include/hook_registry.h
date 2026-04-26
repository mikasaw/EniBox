#ifndef HOOK_REGISTRY_H
#define HOOK_REGISTRY_H
#include <windows.h>
#include <stdint.h>

/* Virtual registry key handle management */
#define MAX_VREG_HANDLES 256
#define VREG_HANDLE_BASE 0xCFFF0000

/* Virtual registry value entry */
typedef struct _VREG_VALUE {
    char     name[256];        /* Value name */
    uint32_t type;             /* REG_SZ, REG_DWORD, etc. */
    uint8_t* data;             /* Value data */
    uint32_t data_size;        /* Size of data */
} VREG_VALUE;

/* Virtual registry key */
typedef struct _VREG_KEY {
    char        path[512];     /* Full normalized key path (uppercase) */
    VREG_VALUE* values;        /* Array of values */
    uint32_t    value_count;   /* Number of values */
    uint32_t    value_capacity;/* Allocated capacity */
} VREG_KEY;

/* Virtual registry handle (returned to application) */
typedef struct _VREG_HANDLE {
    BOOL     inUse;
    uint32_t key_index;       /* Index into virtual key table */
    HKEY     real_key;        /* Real key if not fully virtual (fallback) */
    BOOL     is_virtual;      /* TRUE if this is a virtual-only key */
} VREG_HANDLE;

/* Hook installation */
int32_t HookRegistry_Install(void);

/* Virtual registry initialization - loads registry data from VFS */
int32_t VReg_Initialize(void);
void VReg_Finalize(void);

/* Check if a registry path is virtualized */
BOOL VReg_IsVirtualKeyA(const char* path);
BOOL VReg_IsVirtualKeyW(const wchar_t* path);

/* Find virtual key by path, returns index or -1 */
int32_t VReg_FindKeyA(const char* path);
int32_t VReg_FindKeyW(const wchar_t* path);

/* Handle management */
VREG_HANDLE* VReg_GetHandle(HKEY hKey);
HKEY VReg_AllocHandle(uint32_t key_index, BOOL is_virtual);
void VReg_FreeHandle(HKEY hKey);
BOOL VReg_IsVirtualHandle(HKEY hKey);

#endif
