/*
 * Registry Virtualization Hooks
 *
 * Hooks RegOpenKeyExA/W, RegQueryValueExA/W, RegCloseKey, RegEnumValueA/W,
 * RegSetValueExA/W to redirect registry access to virtual registry data
 * stored in the VFS. This allows applications that read registry settings
 * to work without actually writing to the system registry.
 */

#include "hook_registry.h"
#include "../deps/MinHook/include/MinHook.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ---- Original function pointers ---- */

typedef LONG (WINAPI *RegOpenKeyExA_t)(HKEY, LPCSTR, DWORD, REGSAM, PHKEY);
typedef LONG (WINAPI *RegOpenKeyExW_t)(HKEY, LPCWSTR, DWORD, REGSAM, PHKEY);
typedef LONG (WINAPI *RegQueryValueExA_t)(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
typedef LONG (WINAPI *RegQueryValueExW_t)(HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
typedef LONG (WINAPI *RegCloseKey_t)(HKEY);
typedef LONG (WINAPI *RegEnumValueA_t)(HKEY, DWORD, LPSTR, LPDWORD, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
typedef LONG (WINAPI *RegEnumValueW_t)(HKEY, DWORD, LPWSTR, LPDWORD, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
typedef LONG (WINAPI *RegSetValueExA_t)(HKEY, LPCSTR, DWORD, DWORD, const BYTE*, DWORD);
typedef LONG (WINAPI *RegSetValueExW_t)(HKEY, LPCWSTR, DWORD, DWORD, const BYTE*, DWORD);

static RegOpenKeyExA_t   g_orig_RegOpenKeyExA   = NULL;
static RegOpenKeyExW_t   g_orig_RegOpenKeyExW   = NULL;
static RegQueryValueExA_t g_orig_RegQueryValueExA = NULL;
static RegQueryValueExW_t g_orig_RegQueryValueExW = NULL;
static RegCloseKey_t     g_orig_RegCloseKey     = NULL;
static RegEnumValueA_t   g_orig_RegEnumValueA   = NULL;
static RegEnumValueW_t   g_orig_RegEnumValueW   = NULL;
static RegSetValueExA_t  g_orig_RegSetValueExA  = NULL;
static RegSetValueExW_t  g_orig_RegSetValueExW  = NULL;

/* ---- Virtual registry state ---- */

#define MAX_VREG_KEYS 512

static VREG_KEY     g_vreg_keys[MAX_VREG_KEYS];
static uint32_t     g_vreg_key_count = 0;
static VREG_HANDLE  g_vreg_handles[MAX_VREG_HANDLES];
static BOOL         g_vreg_initialized = FALSE;

/* ---- Helper: Build full key path from HKEY root + subkey ---- */

static void BuildKeyPathA(char* buf, uint32_t size, HKEY hKey, const char* subKey) {
    const char* root = NULL;
    if (hKey == HKEY_LOCAL_MACHINE)    root = "HKEY_LOCAL_MACHINE";
    else if (hKey == HKEY_CURRENT_USER) root = "HKEY_CURRENT_USER";
    else if (hKey == HKEY_CLASSES_ROOT) root = "HKEY_CLASSES_ROOT";
    else if (hKey == HKEY_USERS)       root = "HKEY_USERS";
    else if (hKey == HKEY_CURRENT_CONFIG) root = "HKEY_CURRENT_CONFIG";

    if (root && subKey)
        sprintf_s(buf, size, "%s\\%s", root, subKey);
    else if (root)
        strncpy_s(buf, size, root, _TRUNCATE);
    else if (subKey)
        strncpy_s(buf, size, subKey, _TRUNCATE);
    else
        buf[0] = '\0';

    /* Normalize to uppercase */
    for (uint32_t i = 0; buf[i]; i++)
        if (buf[i] >= 'a' && buf[i] <= 'z') buf[i] = (char)(buf[i] - 32);
}

static void BuildKeyPathW(wchar_t* buf, uint32_t size, HKEY hKey, const wchar_t* subKey) {
    const wchar_t* root = NULL;
    if (hKey == HKEY_LOCAL_MACHINE)    root = L"HKEY_LOCAL_MACHINE";
    else if (hKey == HKEY_CURRENT_USER) root = L"HKEY_CURRENT_USER";
    else if (hKey == HKEY_CLASSES_ROOT) root = L"HKEY_CLASSES_ROOT";
    else if (hKey == HKEY_USERS)       root = L"HKEY_USERS";
    else if (hKey == HKEY_CURRENT_CONFIG) root = L"HKEY_CURRENT_CONFIG";

    if (root && subKey)
        swprintf_s(buf, size, L"%s\\%s", root, subKey);
    else if (root)
        wcsncpy_s(buf, size, root, _TRUNCATE);
    else if (subKey)
        wcsncpy_s(buf, size, subKey, _TRUNCATE);
    else
        buf[0] = L'\0';

    for (uint32_t i = 0; buf[i]; i++)
        if (buf[i] >= L'a' && buf[i] <= L'z') buf[i] = (wchar_t)(buf[i] - 32);
}

/* ---- Virtual registry key/value management ---- */

int32_t VReg_Initialize(void) {
    if (g_vreg_initialized) return 0;
    memset(g_vreg_keys, 0, sizeof(g_vreg_keys));
    memset(g_vreg_handles, 0, sizeof(g_vreg_handles));
    g_vreg_key_count = 0;
    g_vreg_initialized = TRUE;
    return 0;
}

void VReg_Finalize(void) {
    if (!g_vreg_initialized) return;
    for (uint32_t i = 0; i < g_vreg_key_count; i++) {
        if (g_vreg_keys[i].values) {
            for (uint32_t v = 0; v < g_vreg_keys[i].value_count; v++) {
                if (g_vreg_keys[i].values[v].data)
                    free(g_vreg_keys[i].values[v].data);
            }
            free(g_vreg_keys[i].values);
        }
    }
    memset(g_vreg_keys, 0, sizeof(g_vreg_keys));
    memset(g_vreg_handles, 0, sizeof(g_vreg_handles));
    g_vreg_key_count = 0;
    g_vreg_initialized = FALSE;
}

int32_t VReg_FindKeyA(const char* path) {
    if (!path) return -1;
    char normalized[512];
    strncpy_s(normalized, 512, path, 511);
    for (uint32_t i = 0; normalized[i]; i++)
        if (normalized[i] >= 'a' && normalized[i] <= 'z')
            normalized[i] = (char)(normalized[i] - 32);
    for (uint32_t i = 0; i < g_vreg_key_count; i++) {
        if (strcmp(g_vreg_keys[i].path, normalized) == 0)
            return (int32_t)i;
    }
    return -1;
}

int32_t VReg_FindKeyW(const wchar_t* path) {
    if (!path) return -1;
    char narrow[512];
    WideCharToMultiByte(CP_ACP, 0, path, -1, narrow, 512, NULL, NULL);
    return VReg_FindKeyA(narrow);
}

BOOL VReg_IsVirtualKeyA(const char* path) {
    return VReg_FindKeyA(path) >= 0;
}

BOOL VReg_IsVirtualKeyW(const wchar_t* path) {
    return VReg_FindKeyW(path) >= 0;
}

/* ---- Handle management ---- */

VREG_HANDLE* VReg_GetHandle(HKEY hKey) {
    uintptr_t val = (uintptr_t)hKey;
    if (val < VREG_HANDLE_BASE || val >= VREG_HANDLE_BASE + MAX_VREG_HANDLES)
        return NULL;
    uint32_t idx = (uint32_t)(val - VREG_HANDLE_BASE);
    if (!g_vreg_handles[idx].inUse) return NULL;
    return &g_vreg_handles[idx];
}

HKEY VReg_AllocHandle(uint32_t key_index, BOOL is_virtual) {
    for (uint32_t i = 0; i < MAX_VREG_HANDLES; i++) {
        if (!g_vreg_handles[i].inUse) {
            g_vreg_handles[i].inUse = TRUE;
            g_vreg_handles[i].key_index = key_index;
            g_vreg_handles[i].real_key = NULL;
            g_vreg_handles[i].is_virtual = is_virtual;
            return (HKEY)(uintptr_t)(VREG_HANDLE_BASE + i);
        }
    }
    return NULL;
}

void VReg_FreeHandle(HKEY hKey) {
    VREG_HANDLE* vh = VReg_GetHandle(hKey);
    if (vh) {
        if (vh->real_key && !vh->is_virtual)
            g_orig_RegCloseKey(vh->real_key);
        vh->inUse = FALSE;
    }
}

BOOL VReg_IsVirtualHandle(HKEY hKey) {
    return VReg_GetHandle(hKey) != NULL;
}

/* ---- Hook implementations ---- */

static LONG WINAPI Hook_RegOpenKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD ulOptions,
                                       REGSAM samDesired, PHKEY phkResult)
{
    if (lpSubKey && phkResult) {
        char fullPath[512];
        BuildKeyPathA(fullPath, 512, hKey, lpSubKey);
        int32_t keyIdx = VReg_FindKeyA(fullPath);
        if (keyIdx >= 0) {
            HKEY vh = VReg_AllocHandle((uint32_t)keyIdx, TRUE);
            if (vh) {
                *phkResult = vh;
                return ERROR_SUCCESS;
            }
        }
    }
    return g_orig_RegOpenKeyExA(hKey, lpSubKey, ulOptions, samDesired, phkResult);
}

static LONG WINAPI Hook_RegOpenKeyExW(HKEY hKey, LPCWSTR lpSubKey, DWORD ulOptions,
                                       REGSAM samDesired, PHKEY phkResult)
{
    if (lpSubKey && phkResult) {
        wchar_t fullPath[512];
        BuildKeyPathW(fullPath, 512, hKey, lpSubKey);
        int32_t keyIdx = VReg_FindKeyW(fullPath);
        if (keyIdx >= 0) {
            HKEY vh = VReg_AllocHandle((uint32_t)keyIdx, TRUE);
            if (vh) {
                *phkResult = vh;
                return ERROR_SUCCESS;
            }
        }
    }
    return g_orig_RegOpenKeyExW(hKey, lpSubKey, ulOptions, samDesired, phkResult);
}

static LONG WINAPI Hook_RegQueryValueExA(HKEY hKey, LPCSTR lpValueName,
                                          LPDWORD lpReserved, LPDWORD lpType,
                                          LPBYTE lpData, LPDWORD lpcbData)
{
    VREG_HANDLE* vh = VReg_GetHandle(hKey);
    if (vh && vh->is_virtual && vh->key_index < g_vreg_key_count) {
        VREG_KEY* key = &g_vreg_keys[vh->key_index];
        const char* name = lpValueName ? lpValueName : "";

        for (uint32_t i = 0; i < key->value_count; i++) {
            if (strcmp(key->values[i].name, name) == 0) {
                VREG_VALUE* val = &key->values[i];
                if (lpType) *lpType = val->type;
                if (lpcbData) {
                    if (lpData) {
                        if (val->data_size <= *lpcbData) {
                            memcpy(lpData, val->data, val->data_size);
                            *lpcbData = val->data_size;
                        } else {
                            *lpcbData = val->data_size;
                            return ERROR_MORE_DATA;
                        }
                    } else {
                        *lpcbData = val->data_size;
                    }
                }
                return ERROR_SUCCESS;
            }
        }
        return ERROR_FILE_NOT_FOUND;
    }
    return g_orig_RegQueryValueExA(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);
}

static LONG WINAPI Hook_RegQueryValueExW(HKEY hKey, LPCWSTR lpValueName,
                                          LPDWORD lpReserved, LPDWORD lpType,
                                          LPBYTE lpData, LPDWORD lpcbData)
{
    VREG_HANDLE* vh = VReg_GetHandle(hKey);
    if (vh && vh->is_virtual && vh->key_index < g_vreg_key_count) {
        VREG_KEY* key = &g_vreg_keys[vh->key_index];

        char narrowName[256];
        if (lpValueName)
            WideCharToMultiByte(CP_ACP, 0, lpValueName, -1, narrowName, 256, NULL, NULL);
        else
            narrowName[0] = '\0';

        for (uint32_t i = 0; i < key->value_count; i++) {
            if (strcmp(key->values[i].name, narrowName) == 0) {
                VREG_VALUE* val = &key->values[i];
                if (lpType) *lpType = val->type;
                if (lpcbData) {
                    if (lpData) {
                        if (val->data_size <= *lpcbData) {
                            memcpy(lpData, val->data, val->data_size);
                            *lpcbData = val->data_size;
                        } else {
                            *lpcbData = val->data_size;
                            return ERROR_MORE_DATA;
                        }
                    } else {
                        *lpcbData = val->data_size;
                    }
                }
                return ERROR_SUCCESS;
            }
        }
        return ERROR_FILE_NOT_FOUND;
    }
    return g_orig_RegQueryValueExW(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);
}

static LONG WINAPI Hook_RegCloseKey(HKEY hKey) {
    if (VReg_IsVirtualHandle(hKey)) {
        VReg_FreeHandle(hKey);
        return ERROR_SUCCESS;
    }
    return g_orig_RegCloseKey(hKey);
}

static LONG WINAPI Hook_RegEnumValueA(HKEY hKey, DWORD dwIndex, LPSTR lpValueName,
                                       LPDWORD lpcchValueName, LPDWORD lpReserved,
                                       LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData)
{
    VREG_HANDLE* vh = VReg_GetHandle(hKey);
    if (vh && vh->is_virtual && vh->key_index < g_vreg_key_count) {
        VREG_KEY* key = &g_vreg_keys[vh->key_index];
        if (dwIndex >= key->value_count)
            return ERROR_NO_MORE_ITEMS;

        VREG_VALUE* val = &key->values[dwIndex];
        if (lpValueName && lpcchValueName) {
            strncpy_s(lpValueName, *lpcchValueName, val->name, _TRUNCATE);
            *lpcchValueName = (DWORD)strlen(val->name);
        }
        if (lpType) *lpType = val->type;
        if (lpcbData) {
            if (lpData && val->data_size <= *lpcbData) {
                memcpy(lpData, val->data, val->data_size);
            }
            *lpcbData = val->data_size;
        }
        return ERROR_SUCCESS;
    }
    return g_orig_RegEnumValueA(hKey, dwIndex, lpValueName, lpcchValueName,
                                 lpReserved, lpType, lpData, lpcbData);
}

static LONG WINAPI Hook_RegEnumValueW(HKEY hKey, DWORD dwIndex, LPWSTR lpValueName,
                                       LPDWORD lpcchValueName, LPDWORD lpReserved,
                                       LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData)
{
    VREG_HANDLE* vh = VReg_GetHandle(hKey);
    if (vh && vh->is_virtual && vh->key_index < g_vreg_key_count) {
        VREG_KEY* key = &g_vreg_keys[vh->key_index];
        if (dwIndex >= key->value_count)
            return ERROR_NO_MORE_ITEMS;

        VREG_VALUE* val = &key->values[dwIndex];
        if (lpValueName && lpcchValueName) {
            MultiByteToWideChar(CP_ACP, 0, val->name, -1, lpValueName, *lpcchValueName);
            *lpcchValueName = (DWORD)wcslen(lpValueName);
        }
        if (lpType) *lpType = val->type;
        if (lpcbData) {
            if (lpData && val->data_size <= *lpcbData) {
                memcpy(lpData, val->data, val->data_size);
            }
            *lpcbData = val->data_size;
        }
        return ERROR_SUCCESS;
    }
    return g_orig_RegEnumValueW(hKey, dwIndex, lpValueName, lpcchValueName,
                                 lpReserved, lpType, lpData, lpcbData);
}

static LONG WINAPI Hook_RegSetValueExA(HKEY hKey, LPCSTR lpValueName,
                                        DWORD Reserved, DWORD dwType,
                                        const BYTE* lpData, DWORD cbData)
{
    VREG_HANDLE* vh = VReg_GetHandle(hKey);
    if (vh && vh->is_virtual && vh->key_index < g_vreg_key_count) {
        VREG_KEY* key = &g_vreg_keys[vh->key_index];
        const char* name = lpValueName ? lpValueName : "";

        /* Find existing value or add new one */
        for (uint32_t i = 0; i < key->value_count; i++) {
            if (strcmp(key->values[i].name, name) == 0) {
                /* Update existing */
                if (key->values[i].data) free(key->values[i].data);
                key->values[i].data = (uint8_t*)malloc(cbData);
                if (key->values[i].data) {
                    memcpy(key->values[i].data, lpData, cbData);
                    key->values[i].data_size = cbData;
                }
                key->values[i].type = dwType;
                return ERROR_SUCCESS;
            }
        }

        /* Add new value */
        if (key->value_count >= key->value_capacity) {
            uint32_t newCap = key->value_capacity ? key->value_capacity * 2 : 4;
            VREG_VALUE* newVals = (VREG_VALUE*)realloc(key->values, newCap * sizeof(VREG_VALUE));
            if (!newVals) return ERROR_NOT_ENOUGH_MEMORY;
            memset(newVals + key->value_capacity, 0, (newCap - key->value_capacity) * sizeof(VREG_VALUE));
            key->values = newVals;
            key->value_capacity = newCap;
        }

        VREG_VALUE* v = &key->values[key->value_count];
        strncpy_s(v->name, 256, name, 255);
        v->type = dwType;
        v->data = (uint8_t*)malloc(cbData);
        if (v->data) {
            memcpy(v->data, lpData, cbData);
            v->data_size = cbData;
        }
        key->value_count++;
        return ERROR_SUCCESS;
    }
    return g_orig_RegSetValueExA(hKey, lpValueName, Reserved, dwType, lpData, cbData);
}

static LONG WINAPI Hook_RegSetValueExW(HKEY hKey, LPCWSTR lpValueName,
                                        DWORD Reserved, DWORD dwType,
                                        const BYTE* lpData, DWORD cbData)
{
    VREG_HANDLE* vh = VReg_GetHandle(hKey);
    if (vh && vh->is_virtual) {
        char narrowName[256];
        if (lpValueName)
            WideCharToMultiByte(CP_ACP, 0, lpValueName, -1, narrowName, 256, NULL, NULL);
        else
            narrowName[0] = '\0';
        /* Delegate to A version logic */
        return Hook_RegSetValueExA(hKey, narrowName, Reserved, dwType, lpData, cbData);
    }
    return g_orig_RegSetValueExW(hKey, lpValueName, Reserved, dwType, lpData, cbData);
}

/* ---- Installation ---- */

int32_t HookRegistry_Install(void) {
    VReg_Initialize();

    if (MH_CreateHook(&RegOpenKeyExA, &Hook_RegOpenKeyExA,
        (void**)&g_orig_RegOpenKeyExA) != MH_OK) return -1;
    if (MH_CreateHook(&RegOpenKeyExW, &Hook_RegOpenKeyExW,
        (void**)&g_orig_RegOpenKeyExW) != MH_OK) return -2;
    if (MH_CreateHook(&RegQueryValueExA, &Hook_RegQueryValueExA,
        (void**)&g_orig_RegQueryValueExA) != MH_OK) return -3;
    if (MH_CreateHook(&RegQueryValueExW, &Hook_RegQueryValueExW,
        (void**)&g_orig_RegQueryValueExW) != MH_OK) return -4;
    if (MH_CreateHook(&RegCloseKey, &Hook_RegCloseKey,
        (void**)&g_orig_RegCloseKey) != MH_OK) return -5;
    if (MH_CreateHook(&RegEnumValueA, &Hook_RegEnumValueA,
        (void**)&g_orig_RegEnumValueA) != MH_OK) return -6;
    if (MH_CreateHook(&RegEnumValueW, &Hook_RegEnumValueW,
        (void**)&g_orig_RegEnumValueW) != MH_OK) return -7;
    if (MH_CreateHook(&RegSetValueExA, &Hook_RegSetValueExA,
        (void**)&g_orig_RegSetValueExA) != MH_OK) return -8;
    if (MH_CreateHook(&RegSetValueExW, &Hook_RegSetValueExW,
        (void**)&g_orig_RegSetValueExW) != MH_OK) return -9;

    return 0;
}


