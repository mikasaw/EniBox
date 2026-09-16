/*
 * Registry Virtualization Hooks
 *
 * Hooks RegOpenKeyExA/W, RegCreateKeyExA/W, RegQueryValueExA/W, RegCloseKey,
 * RegEnumValueA/W, RegSetValueExA/W, RegDeleteValueA/W, RegDeleteKeyA/W,
 * RegDeleteTreeA/W, RegDeleteKeyExA/W to redirect registry access to virtual
 * registry data stored in the VFS. This allows applications that read/write/
 * delete registry settings to work without actually touching the system
 * registry (19 hooks).
 *
 * Scope rule: every virtualized key path declares a scope root; the whole
 * subtree below it (prefix rule) is virtualized, including keys created at
 * runtime. Writes are persisted as a whole-store snapshot to the sidecar
 * file "<packed exe path>.vreg.bin" (last-write-wins, no locking); on
 * startup the sidecar replaces the presets entirely, deleting it resets
 * to the preset values.
 */

#include "hook_registry.h"
#include "../deps/MinHook/include/MinHook.h"
#include "diag_file.h"
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
typedef LONG (WINAPI *RegCreateKeyExA_t)(HKEY, LPCSTR, DWORD, LPSTR, DWORD, REGSAM,
    const SECURITY_ATTRIBUTES*, PHKEY, LPDWORD);
typedef LONG (WINAPI *RegCreateKeyExW_t)(HKEY, LPCWSTR, DWORD, LPWSTR, DWORD, REGSAM,
    const SECURITY_ATTRIBUTES*, PHKEY, LPDWORD);
typedef LONG (WINAPI *RegDeleteValueA_t)(HKEY, LPCSTR);
typedef LONG (WINAPI *RegDeleteValueW_t)(HKEY, LPCWSTR);
typedef LONG (WINAPI *RegDeleteKeyA_t)(HKEY, LPCSTR);
typedef LONG (WINAPI *RegDeleteKeyW_t)(HKEY, LPCWSTR);
typedef LONG (WINAPI *RegDeleteTreeA_t)(HKEY, LPCSTR);
typedef LONG (WINAPI *RegDeleteTreeW_t)(HKEY, LPCWSTR);
typedef LONG (WINAPI *RegDeleteKeyExA_t)(HKEY, LPCSTR, REGSAM, DWORD);
typedef LONG (WINAPI *RegDeleteKeyExW_t)(HKEY, LPCWSTR, REGSAM, DWORD);

static RegOpenKeyExA_t   g_orig_RegOpenKeyExA   = NULL;
static RegOpenKeyExW_t   g_orig_RegOpenKeyExW   = NULL;
static RegQueryValueExA_t g_orig_RegQueryValueExA = NULL;
static RegQueryValueExW_t g_orig_RegQueryValueExW = NULL;
static RegCloseKey_t     g_orig_RegCloseKey     = NULL;
static RegEnumValueA_t   g_orig_RegEnumValueA   = NULL;
static RegEnumValueW_t   g_orig_RegEnumValueW   = NULL;
static RegSetValueExA_t  g_orig_RegSetValueExA  = NULL;
static RegSetValueExW_t  g_orig_RegSetValueExW  = NULL;
static RegCreateKeyExA_t g_orig_RegCreateKeyExA = NULL;
static RegCreateKeyExW_t g_orig_RegCreateKeyExW = NULL;
static RegDeleteValueA_t g_orig_RegDeleteValueA = NULL;
static RegDeleteValueW_t g_orig_RegDeleteValueW = NULL;
static RegDeleteKeyA_t   g_orig_RegDeleteKeyA   = NULL;
static RegDeleteKeyW_t   g_orig_RegDeleteKeyW   = NULL;
static RegDeleteTreeA_t  g_orig_RegDeleteTreeA  = NULL;
static RegDeleteTreeW_t  g_orig_RegDeleteTreeW  = NULL;
static RegDeleteKeyExA_t g_orig_RegDeleteKeyExA = NULL;
static RegDeleteKeyExW_t g_orig_RegDeleteKeyExW = NULL;

/* ---- Virtual registry state ---- */

#define MAX_VREG_KEYS 512

static VREG_KEY     g_vreg_keys[MAX_VREG_KEYS];
static uint32_t     g_vreg_key_count = 0;
static VREG_HANDLE  g_vreg_handles[MAX_VREG_HANDLES];
static BOOL         g_vreg_initialized = FALSE;

/* 键表变更与 sidecar 落盘共用一把锁（CRITICAL_SECTION 可重入）。
 * 查询/枚举路径保持与既有实现一致的无锁读取。 */
static CRITICAL_SECTION g_vreg_lock;
static BOOL         g_vreg_lock_init = FALSE;
static wchar_t      g_vreg_sidecar_path[1024] = {0};

/* ---- Helper: Build full key path from HKEY root + subkey ---- */

/* 预定义根键按低 32 位比较：应用可能传零扩展（如 .NET IntPtr）或符号扩展
 * （SDK 常量 (LONG)0x80000001→ULONG_PTR）两种形态，精确指针比较会失配。 */
static uint32_t RegHandleId(HKEY hKey) {
    return (uint32_t)(uintptr_t)hKey;
}

static void BuildKeyPathA(char* buf, uint32_t size, HKEY hKey, const char* subKey) {
    const char* root = NULL;
    uint32_t hk = RegHandleId(hKey);
    if (hk == 0x80000002)      root = "HKEY_LOCAL_MACHINE";
    else if (hk == 0x80000001) root = "HKEY_CURRENT_USER";
    else if (hk == 0x80000000) root = "HKEY_CLASSES_ROOT";
    else if (hk == 0x80000003) root = "HKEY_USERS";
    else if (hk == 0x80000005) root = "HKEY_CURRENT_CONFIG";
    else {
        /* 虚拟句柄：以句柄指向的键路径为根——open/create/delete 以虚拟句柄
         * 为父句柄时不再落穿透（墓碑句柄解析为空路径，走原 subKey 回退） */
        VREG_HANDLE* vh = VReg_GetHandle(hKey);
        if (vh && vh->is_virtual && vh->key_index < g_vreg_key_count &&
            g_vreg_keys[vh->key_index].path[0] != '\0') {
            root = g_vreg_keys[vh->key_index].path;
        }
    }

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
    wchar_t wideRoot[512] = {0}; /* 函数域存活：虚拟句柄根路径的宽字符转换目标 */
    uint32_t hk = RegHandleId(hKey);
    if (hk == 0x80000002)      root = L"HKEY_LOCAL_MACHINE";
    else if (hk == 0x80000001) root = L"HKEY_CURRENT_USER";
    else if (hk == 0x80000000) root = L"HKEY_CLASSES_ROOT";
    else if (hk == 0x80000003) root = L"HKEY_USERS";
    else if (hk == 0x80000005) root = L"HKEY_CURRENT_CONFIG";
    else {
        /* 虚拟句柄：宽/窄路径等价（键路径为 ANSI 大写），转宽拼接 */
        VREG_HANDLE* vh = VReg_GetHandle(hKey);
        if (vh && vh->is_virtual && vh->key_index < g_vreg_key_count &&
            g_vreg_keys[vh->key_index].path[0] != '\0') {
            if (MultiByteToWideChar(CP_ACP, 0, g_vreg_keys[vh->key_index].path, -1,
                                    wideRoot, 512) > 0) {
                root = wideRoot;
            }
        }
    }

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
    if (!g_vreg_lock_init) {
        InitializeCriticalSection(&g_vreg_lock);
        g_vreg_lock_init = TRUE;
    }
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
    if (path == NULL || path[0] == '\0') return -1; /* 空串不得命中墓碑槽位 */
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

/* ---- 作用域（前缀子树规则）与动态建键 ----
 * 作用域根 = 当前虚拟存储中的每个键路径（首启来自预置值，其后来自 sidecar，
 * 而 sidecar 键必然位于预置子树内）。路径 P 在作用域内 ⇔ P 等于某根，
 * 或以「某根 + '\'」为前缀。根的父链与兄弟路径（如根...\APPX）不在作用域内。 */

static BOOL VReg_IsInScopeA(const char* pathUpper) {
    if (pathUpper == NULL || pathUpper[0] == '\0') return FALSE;
    for (uint32_t i = 0; i < g_vreg_key_count; i++) {
        const char* root = g_vreg_keys[i].path;
        if (root[0] == '\0') continue; /* 墓碑槽位 */
        size_t len = strlen(root);
        if (strncmp(pathUpper, root, len) == 0 &&
            (pathUpper[len] == '\0' || pathUpper[len] == '\\'))
            return TRUE;
    }
    return FALSE;
}

/* 查找或创建虚拟键（调用方需持有 g_vreg_lock）。返回索引或 -1（表满/分配失败）。 */
static int32_t VReg_EnsureKeyA(const char* pathUpper) {
    if (pathUpper == NULL || pathUpper[0] == '\0') return -1;
    int32_t idx = VReg_FindKeyA(pathUpper);
    if (idx >= 0) return idx;
    if (g_vreg_key_count >= MAX_VREG_KEYS) return -1;
    VREG_KEY* key = &g_vreg_keys[g_vreg_key_count];
    memset(key, 0, sizeof(*key));
    strncpy_s(key->path, sizeof(key->path), pathUpper, _TRUNCATE);
    key->value_capacity = 4;
    key->values = (VREG_VALUE*)calloc(key->value_capacity, sizeof(VREG_VALUE));
    if (!key->values) return -1;
    return (int32_t)g_vreg_key_count++;
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

/* ---- Registry preset loading (VFS blob v2 'EREG' region) ---- */

#define VREG_REG_MAGIC 0x47455245u /* 'EREG' */
#define VREG_REG_MAX_KEYS     MAX_VREG_KEYS
#define VREG_REG_MAX_VALUES   64

static uint32_t VReg_ReadU32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void VReg_UpperA(char* s) {
    for (; *s; ++s)
        if (*s >= 'a' && *s <= 'z') *s = (char)(*s - 32);
}

/* 载入一个键。raw_data=FALSE：预置区模式，REG_SZ/EXPAND_SZ 数据按 UTF-16LE
 * 读入并转 CP_ACP（A 族查询按 ANSI 读取）；raw_data=TRUE：sidecar 模式，
 * 数据按写入时的原始字节原样拷贝（与写入当次进程内可见性完全一致）。
 * 返回 0 成功；负数为格式/越界错误（调用方整体放弃）。 */
static int32_t VReg_LoadKey(const uint8_t** cursor, const uint8_t* end, BOOL raw_data) {
    const uint8_t* p = *cursor;
    if (p + 4 > end) return -10;
    uint32_t keyChars = VReg_ReadU32(p); p += 4;
    if (keyChars == 0 || keyChars >= 512 || p + keyChars * 2 > end) return -11;
    wchar_t keyW[512];
    memcpy(keyW, p, keyChars * 2); keyW[keyChars] = 0; p += keyChars * 2;

    char pathA[512];
    if (!WideCharToMultiByte(CP_ACP, 0, keyW, -1, pathA, sizeof(pathA), NULL, NULL)) return -12;
    VReg_UpperA(pathA);

    if (p + 4 > end) return -13;
    uint32_t valueCount = VReg_ReadU32(p); p += 4;
    if (raw_data) {
        /* sidecar 允许运行期累积的任意值数；双重上限：绝对上限防 crafted
         * 文件巨额 calloc，剩余字节约束（每条序列化值至少 12 字节）防越界 */
        if (valueCount > 65536 || valueCount > (uint32_t)((end - p) / 12) + 1) return -14;
    } else {
        if (valueCount > VREG_REG_MAX_VALUES) return -14;
    }

    if (g_vreg_key_count >= VREG_REG_MAX_KEYS) return -15;
    /* 同路径重复条目（防御旧打包器/手造 blob）：值并入既有槽位而非新建，
     * 否则 FindKey 只命中第一个，后续条目的值不可达 */
    int32_t existing = VReg_FindKeyA(pathA);
    VREG_KEY* key;
    if (existing >= 0) {
        key = &g_vreg_keys[existing];
    } else {
        key = &g_vreg_keys[g_vreg_key_count++];
        memset(key, 0, sizeof(*key));
        strncpy_s(key->path, sizeof(key->path), pathA, _TRUNCATE);
    }
    if (key->value_capacity < key->value_count + valueCount) {
        uint32_t newCap = key->value_count + valueCount;
        VREG_VALUE* nv = (VREG_VALUE*)realloc(key->values, newCap * sizeof(VREG_VALUE));
        if (!nv) return -16;
        memset(nv + key->value_count, 0, (newCap - key->value_count) * sizeof(VREG_VALUE));
        key->values = nv;
        key->value_capacity = newCap;
    }

    for (uint32_t v = 0; v < valueCount; v++) {
        if (p + 4 > end) return -17;
        uint32_t nameChars = VReg_ReadU32(p); p += 4;
        if (nameChars >= 256 || p + nameChars * 2 > end) return -17;
        wchar_t nameW[256];
        memcpy(nameW, p, nameChars * 2); nameW[nameChars] = 0; p += nameChars * 2;

        if (p + 8 > end) return -18;
        uint32_t type = VReg_ReadU32(p); p += 4;
        uint32_t dataBytes = VReg_ReadU32(p); p += 4;
        if (p + dataBytes > end) return -18;

        VREG_VALUE* val = &key->values[key->value_count];
        char nameA[256];
        if (!WideCharToMultiByte(CP_ACP, 0, nameW, -1, nameA, sizeof(nameA), NULL, NULL)) return -19;
        strncpy_s(val->name, sizeof(val->name), nameA, _TRUNCATE);
        val->type = type;
        val->data = NULL;
        val->data_size = 0;

        if (dataBytes) {
            if (!raw_data && (type == REG_SZ || type == REG_EXPAND_SZ)) {
                /* 预置区：文件内 UTF-16LE → 内存内 CP_ACP（A 族查询按 ANSI 读取） */
                int need = WideCharToMultiByte(CP_ACP, 0, (const wchar_t*)p,
                                               dataBytes / 2, NULL, 0, NULL, NULL);
                if (need > 0) {
                    val->data = (uint8_t*)malloc((size_t)need);
                    if (val->data) {
                        WideCharToMultiByte(CP_ACP, 0, (const wchar_t*)p, dataBytes / 2,
                                            (char*)val->data, need, NULL, NULL);
                        val->data_size = (uint32_t)need;
                    }
                }
            } else {
                /* sidecar / 二进制类：原样拷贝，保持与写入时一致的字节 */
                val->data = (uint8_t*)malloc(dataBytes);
                if (val->data) {
                    memcpy(val->data, p, dataBytes);
                    val->data_size = dataBytes;
                }
            }
            p += dataBytes;
        }
        key->value_count++;
    }
    *cursor = p;
    return 0;
}

static int32_t VReg_ParseRegion(const uint8_t* blob, uint32_t size, BOOL raw_data) {
    if (!blob || size < 8) return -1;
    if (VReg_ReadU32(blob) != VREG_REG_MAGIC) return -2;
    uint32_t count = VReg_ReadU32(blob + 4);
    if (count > VREG_REG_MAX_KEYS) return -3;
    if (VReg_Initialize() != 0) return -4;

    const uint8_t* cursor = blob + 8;
    const uint8_t* end = blob + size;
    int32_t result = 0;
    for (uint32_t k = 0; k < count; k++) {
        result = VReg_LoadKey(&cursor, end, raw_data);
        if (result != 0) break;
    }
    if (result != 0) {
        /* 载入失败：清空已载入内容，registry 退化为空存储（全透传） */
        VReg_Finalize();
        VReg_Initialize();
    }
    return result;
}

int32_t VReg_Preload(const uint8_t* blob, uint32_t size) {
    return VReg_ParseRegion(blob, size, FALSE);
}

/* ---- 持久化 sidecar（<封包产物路径>.vreg.bin）----
 * 文件布局：[8B magic "ENIVREG1"][u32 bodySize][body][u32 crc32(body)]。
 * body 即 'EREG' 区格式（含 magic），数据字节为写入时的原始字节；
 * 读取时 CRC 校验失败或解析失败一律视为 sidecar 不存在（回退预置值）。 */

static const uint8_t VREG_SIDECAR_MAGIC[8] =
    { 'E', 'N', 'I', 'V', 'R', 'E', 'G', '1' };

static uint32_t VReg_Crc32(const uint8_t* data, uint32_t size) {
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (0u - (crc & 1)));
        }
    }
    return crc ^ 0xFFFFFFFF;
}

typedef struct {
    uint8_t* data;
    uint32_t size;
    uint32_t cap;
    BOOL     failed;
} VREG_BUF;

static BOOL VReg_BufReserve(VREG_BUF* b, uint32_t extra) {
    if (b->failed) return FALSE;
    /* 64 位比较防回绕：size 逼近上限时 uint32 加法会溢出为小值 */
    if ((uint64_t)b->size + extra <= b->cap) return TRUE;
    uint32_t ncap = b->cap ? b->cap : 4096;
    while ((uint64_t)ncap < (uint64_t)b->size + extra) {
        if (ncap > 0x40000000u) { b->failed = TRUE; return FALSE; }
        ncap *= 2;
    }
    uint8_t* nd = (uint8_t*)realloc(b->data, ncap);
    if (!nd) { b->failed = TRUE; return FALSE; }
    b->data = nd;
    b->cap = ncap;
    return TRUE;
}

static BOOL VReg_BufU32(VREG_BUF* b, uint32_t v) {
    if (!VReg_BufReserve(b, 4)) return FALSE;
    b->data[b->size++] = (uint8_t)(v & 0xFF);
    b->data[b->size++] = (uint8_t)((v >> 8) & 0xFF);
    b->data[b->size++] = (uint8_t)((v >> 16) & 0xFF);
    b->data[b->size++] = (uint8_t)((v >> 24) & 0xFF);
    return TRUE;
}

/* 写 u32 字符数 + ANSI→UTF-16LE 字符串（不含尾 NUL），与解析端语义一致 */
static BOOL VReg_BufUtf16StrA(VREG_BUF* b, const char* a) {
    int wlen = MultiByteToWideChar(CP_ACP, 0, a, -1, NULL, 0);
    if (wlen <= 0) { b->failed = TRUE; return FALSE; }
    uint32_t chars = (uint32_t)wlen - 1;
    if (!VReg_BufReserve(b, 4 + chars * 2 + 2)) return FALSE;
    uint8_t* out = b->data + b->size;
    out[0] = (uint8_t)(chars & 0xFF);
    out[1] = (uint8_t)((chars >> 8) & 0xFF);
    out[2] = (uint8_t)((chars >> 16) & 0xFF);
    out[3] = (uint8_t)((chars >> 24) & 0xFF);
    MultiByteToWideChar(CP_ACP, 0, a, -1, (wchar_t*)(out + 4), wlen);
    b->size += 4 + chars * 2;
    return TRUE;
}

static void VReg_StoreU32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

/* 整库序列化为 'EREG' 区格式（含 magic）。锁内只读快照。
 * 墓碑槽位（path[0]=0，已删除键）不写入。 */
static BOOL VReg_SerializeStore(VREG_BUF* body) {
    EnterCriticalSection(&g_vreg_lock);
    uint32_t live = 0;
    for (uint32_t i = 0; i < g_vreg_key_count; i++) {
        if (g_vreg_keys[i].path[0] != '\0') live++;
    }
    BOOL ok = VReg_BufU32(body, VREG_REG_MAGIC) && VReg_BufU32(body, live);
    for (uint32_t i = 0; ok && i < g_vreg_key_count; i++) {
        VREG_KEY* key = &g_vreg_keys[i];
        if (key->path[0] == '\0') continue;
        ok = VReg_BufUtf16StrA(body, key->path) && VReg_BufU32(body, key->value_count);
        for (uint32_t v = 0; ok && v < key->value_count; v++) {
            VREG_VALUE* val = &key->values[v];
            ok = VReg_BufUtf16StrA(body, val->name)
              && VReg_BufU32(body, val->type)
              && VReg_BufU32(body, val->data_size);
            if (ok && val->data_size && val->data) {
                if (VReg_BufReserve(body, val->data_size)) {
                    memcpy(body->data + body->size, val->data, val->data_size);
                    body->size += val->data_size;
                } else {
                    ok = FALSE;
                }
            }
        }
    }
    LeaveCriticalSection(&g_vreg_lock);
    return ok && !body->failed;
}

void VReg_SetSidecarPathW(const wchar_t* path) {
    if (!path || !path[0]) return;
    wcsncpy_s(g_vreg_sidecar_path, 1024, path, _TRUNCATE);
}

BOOL VReg_LoadSidecar(void) {
    if (!g_vreg_sidecar_path[0]) return FALSE;
    /* 共享读写打开：兄弟实例 SAVE（独占写）瞬间不误判为无档；
     * 读到部分内容时由 CRC/解析兜底回退预置值 */
    HANDLE h = CreateFileW(g_vreg_sidecar_path, GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE; /* 首启无 sidecar：保留预置值 */

    BOOL ok = FALSE;
    uint8_t* buf = NULL;
    LARGE_INTEGER fileSize;
    do {
        if (!GetFileSizeEx(h, &fileSize) || fileSize.QuadPart < 16 ||
            fileSize.QuadPart > 0x04100000) break; /* 上限 64MB + 头尾 */
        uint32_t size = (uint32_t)fileSize.QuadPart;
        buf = (uint8_t*)malloc(size);
        if (!buf) break;
        DWORD read = 0;
        if (!ReadFile(h, buf, size, &read, NULL) || read != size) break;
        if (memcmp(buf, VREG_SIDECAR_MAGIC, 8) != 0) break;
        uint32_t bodySize = VReg_ReadU32(buf + 8);
        if (bodySize < 8 || bodySize != size - 16) break;
        uint32_t storedCrc = VReg_ReadU32(buf + 12 + bodySize);
        if (storedCrc != VReg_Crc32(buf + 12, bodySize)) break;
        /* 持久化优先：整体替换预置值（预置仅首启生效） */
        VReg_Finalize();
        VReg_Initialize();
        if (VReg_ParseRegion(buf + 12, bodySize, TRUE) != 0) break;
        ok = TRUE;
    } while (0);
    free(buf);
    CloseHandle(h);
    EniBox_DiagLine(ok ? "vreg sidecar: loaded" : "vreg sidecar: absent-or-invalid");
    return ok;
}

BOOL VReg_SaveSidecar(void) {
    if (!g_vreg_sidecar_path[0]) return FALSE;
    VREG_BUF body = {0};
    if (!VReg_SerializeStore(&body)) {
        free(body.data);
        EniBox_DiagLine("vreg save: serialize failed");
        return FALSE;
    }
    uint32_t total = 8 + 4 + body.size + 4;
    uint8_t* file = (uint8_t*)malloc(total);
    if (!file) {
        free(body.data);
        return FALSE;
    }
    memcpy(file, VREG_SIDECAR_MAGIC, 8);
    VReg_StoreU32(file + 8, body.size);
    memcpy(file + 12, body.data, body.size);
    VReg_StoreU32(file + 12 + body.size, VReg_Crc32(body.data, body.size));
    free(body.data);

    HANDLE h = CreateFileW(g_vreg_sidecar_path, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        free(file);
        EniBox_DiagLine("vreg save: open failed");
        return FALSE;
    }
    DWORD written = 0;
    BOOL ok = WriteFile(h, file, total, &written, NULL) && written == total;
    CloseHandle(h);
    free(file);
    if (!ok) EniBox_DiagLine("vreg save: write failed");
    return ok;
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
        } else if (VReg_IsInScopeA(fullPath)) {
            /* 作用域子树内未入存的键：以真实注册表探测存在性（沿用应用的
             * 访问权限语义）；存在则建全虚拟键接管（真实值被遮蔽且不再回读），
             * 不存在则原样返回真实错误码（保持 open 不建键语义）。 */
            HKEY probe = NULL;
            LONG rc = g_orig_RegOpenKeyExA(hKey, lpSubKey, ulOptions, samDesired, &probe);
            if (rc != ERROR_SUCCESS) return rc;
            g_orig_RegCloseKey(probe);
            EnterCriticalSection(&g_vreg_lock);
            keyIdx = VReg_EnsureKeyA(fullPath);
            LeaveCriticalSection(&g_vreg_lock);
            if (keyIdx >= 0) {
                HKEY vh = VReg_AllocHandle((uint32_t)keyIdx, TRUE);
                if (vh) {
                    *phkResult = vh;
                    return ERROR_SUCCESS;
                }
            }
            return ERROR_NOT_ENOUGH_MEMORY;
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
        char fullPathA[512] = {0};
        WideCharToMultiByte(CP_ACP, 0, fullPath, -1, fullPathA, 512, NULL, NULL);
        int32_t keyIdx = VReg_FindKeyW(fullPath);
        if (keyIdx >= 0) {
            HKEY vh = VReg_AllocHandle((uint32_t)keyIdx, TRUE);
            if (vh) {
                *phkResult = vh;
                return ERROR_SUCCESS;
            }
        } else if (VReg_IsInScopeA(fullPathA)) {
            HKEY probe = NULL;
            LONG rc = g_orig_RegOpenKeyExW(hKey, lpSubKey, ulOptions, samDesired, &probe);
            if (rc != ERROR_SUCCESS) return rc;
            g_orig_RegCloseKey(probe);
            EnterCriticalSection(&g_vreg_lock);
            keyIdx = VReg_EnsureKeyA(fullPathA);
            LeaveCriticalSection(&g_vreg_lock);
            if (keyIdx >= 0) {
                HKEY vh = VReg_AllocHandle((uint32_t)keyIdx, TRUE);
                if (vh) {
                    *phkResult = vh;
                    return ERROR_SUCCESS;
                }
            }
            return ERROR_NOT_ENOUGH_MEMORY;
        }
    }
    return g_orig_RegOpenKeyExW(hKey, lpSubKey, ulOptions, samDesired, phkResult);
}

/* 作用域子树内 RegCreateKeyEx 的虚拟建键（A/W 共用；fullPath 为大写 ANSI）。
 * 新建空键立即落盘，保证跨启动「已创建」语义。 */
static LONG VReg_CreateVirtualKeyA(const char* fullPath, PHKEY phkResult,
                                   LPDWORD lpdwDisposition) {
    EnterCriticalSection(&g_vreg_lock);
    int32_t keyIdx = VReg_FindKeyA(fullPath);
    DWORD disp = REG_OPENED_EXISTING_KEY;
    if (keyIdx < 0) {
        keyIdx = VReg_EnsureKeyA(fullPath);
        disp = REG_CREATED_NEW_KEY;
    }
    LeaveCriticalSection(&g_vreg_lock);
    if (keyIdx < 0) return ERROR_NOT_ENOUGH_MEMORY;
    HKEY vh = VReg_AllocHandle((uint32_t)keyIdx, TRUE);
    if (!vh) return ERROR_NOT_ENOUGH_MEMORY;
    *phkResult = vh;
    if (lpdwDisposition) *lpdwDisposition = disp;
    if (disp == REG_CREATED_NEW_KEY) VReg_SaveSidecar();
    return ERROR_SUCCESS;
}

static LONG WINAPI Hook_RegCreateKeyExA(HKEY hKey, LPCSTR lpSubKey, DWORD Reserved,
                                        LPSTR lpClass, DWORD dwOptions, REGSAM samDesired,
                                        const SECURITY_ATTRIBUTES* lpSecurityAttributes,
                                        PHKEY phkResult, LPDWORD lpdwDisposition)
{
    if (lpSubKey && phkResult) {
        char fullPath[512];
        BuildKeyPathA(fullPath, 512, hKey, lpSubKey);
        if (VReg_IsInScopeA(fullPath))
            return VReg_CreateVirtualKeyA(fullPath, phkResult, lpdwDisposition);
    }
    return g_orig_RegCreateKeyExA(hKey, lpSubKey, Reserved, lpClass, dwOptions,
                                  samDesired, lpSecurityAttributes, phkResult,
                                  lpdwDisposition);
}

static LONG WINAPI Hook_RegCreateKeyExW(HKEY hKey, LPCWSTR lpSubKey, DWORD Reserved,
                                        LPWSTR lpClass, DWORD dwOptions, REGSAM samDesired,
                                        const SECURITY_ATTRIBUTES* lpSecurityAttributes,
                                        PHKEY phkResult, LPDWORD lpdwDisposition)
{
    if (lpSubKey && phkResult) {
        char fullPath[512] = {0};
        wchar_t fullPathW[512];
        BuildKeyPathW(fullPathW, 512, hKey, lpSubKey);
        WideCharToMultiByte(CP_ACP, 0, fullPathW, -1, fullPath, 512, NULL, NULL);
        if (VReg_IsInScopeA(fullPath))
            return VReg_CreateVirtualKeyA(fullPath, phkResult, lpdwDisposition);
    }
    return g_orig_RegCreateKeyExW(hKey, lpSubKey, Reserved, lpClass, dwOptions,
                                  samDesired, lpSecurityAttributes, phkResult,
                                  lpdwDisposition);
}

static LONG WINAPI Hook_RegQueryValueExA(HKEY hKey, LPCSTR lpValueName,
                                          LPDWORD lpReserved, LPDWORD lpType,
                                          LPBYTE lpData, LPDWORD lpcbData)
{
    VREG_HANDLE* vh = VReg_GetHandle(hKey);
    if (vh && vh->is_virtual && vh->key_index < g_vreg_key_count) {
        /* 锁内读：与删除（free）互斥，避免无锁读 freed 内存 */
        EnterCriticalSection(&g_vreg_lock);
        LONG result = ERROR_FILE_NOT_FOUND;
        VREG_KEY* key = &g_vreg_keys[vh->key_index];
        const char* name = lpValueName ? lpValueName : "";
        if (key->path[0] == '\0') {
            result = ERROR_KEY_DELETED; /* 已删除键的遗留句柄 */
        } else {
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
                                result = ERROR_MORE_DATA;
                                break;
                            }
                        } else {
                            *lpcbData = val->data_size;
                        }
                    }
                    result = ERROR_SUCCESS;
                    break;
                }
            }
        }
        LeaveCriticalSection(&g_vreg_lock);
        return result;
    }
    return g_orig_RegQueryValueExA(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);
}

static LONG WINAPI Hook_RegQueryValueExW(HKEY hKey, LPCWSTR lpValueName,
                                          LPDWORD lpReserved, LPDWORD lpType,
                                          LPBYTE lpData, LPDWORD lpcbData)
{
    VREG_HANDLE* vh = VReg_GetHandle(hKey);
    if (vh && vh->is_virtual && vh->key_index < g_vreg_key_count) {
        EnterCriticalSection(&g_vreg_lock);
        LONG result = ERROR_FILE_NOT_FOUND;
        VREG_KEY* key = &g_vreg_keys[vh->key_index];

        char narrowName[256];
        if (lpValueName)
            WideCharToMultiByte(CP_ACP, 0, lpValueName, -1, narrowName, 256, NULL, NULL);
        else
            narrowName[0] = '\0';

        if (key->path[0] == '\0') {
            result = ERROR_KEY_DELETED; /* 已删除键的遗留句柄 */
        } else {
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
                                result = ERROR_MORE_DATA;
                                break;
                            }
                        } else {
                            *lpcbData = val->data_size;
                        }
                    }
                    result = ERROR_SUCCESS;
                    break;
                }
            }
        }
        LeaveCriticalSection(&g_vreg_lock);
        return result;
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
        EnterCriticalSection(&g_vreg_lock);
        LONG result;
        VREG_KEY* key = &g_vreg_keys[vh->key_index];
        if (key->path[0] == '\0') {
            result = ERROR_KEY_DELETED; /* 已删除键的遗留句柄 */
        } else if (dwIndex >= key->value_count) {
            result = ERROR_NO_MORE_ITEMS;
        } else {
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
            result = ERROR_SUCCESS;
        }
        LeaveCriticalSection(&g_vreg_lock);
        return result;
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
        EnterCriticalSection(&g_vreg_lock);
        LONG result;
        VREG_KEY* key = &g_vreg_keys[vh->key_index];
        if (key->path[0] == '\0') {
            result = ERROR_KEY_DELETED; /* 已删除键的遗留句柄 */
        } else if (dwIndex >= key->value_count) {
            result = ERROR_NO_MORE_ITEMS;
        } else {
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
            result = ERROR_SUCCESS;
        }
        LeaveCriticalSection(&g_vreg_lock);
        return result;
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
        LONG result;

        /* 变更段持锁：序列化线程不得在 free→malloc 窗口读到悬垂指针
         * （CS 可重入，随后的 SaveSidecar 再入安全）。墓碑守卫必须在锁内
         * 复查——锁外检查与并发删除存在窗口。 */
        EnterCriticalSection(&g_vreg_lock);
        if (key->path[0] == '\0') {
            LeaveCriticalSection(&g_vreg_lock);
            return ERROR_KEY_DELETED;
        }
        /* Find existing value or add new one */
        result = -1;
        for (uint32_t i = 0; result == -1 && i < key->value_count; i++) {
            if (strcmp(key->values[i].name, name) == 0) {
                /* Update existing */
                if (key->values[i].data) free(key->values[i].data);
                key->values[i].data = (uint8_t*)malloc(cbData ? cbData : 1);
                if (key->values[i].data) {
                    if (cbData) memcpy(key->values[i].data, lpData, cbData);
                    key->values[i].data_size = cbData;
                } else {
                    key->values[i].data_size = 0;
                    result = ERROR_NOT_ENOUGH_MEMORY;
                }
                if (result == -1) {
                    key->values[i].type = dwType;
                    result = ERROR_SUCCESS;
                }
            }
        }

        if (result == -1) {
            /* Add new value */
            if (key->value_count >= key->value_capacity) {
                uint32_t newCap = key->value_capacity ? key->value_capacity * 2 : 4;
                VREG_VALUE* newVals = (VREG_VALUE*)realloc(key->values, newCap * sizeof(VREG_VALUE));
                if (!newVals) {
                    LeaveCriticalSection(&g_vreg_lock);
                    return ERROR_NOT_ENOUGH_MEMORY;
                }
                memset(newVals + key->value_capacity, 0, (newCap - key->value_capacity) * sizeof(VREG_VALUE));
                key->values = newVals;
                key->value_capacity = newCap;
            }

            VREG_VALUE* v = &key->values[key->value_count];
            strncpy_s(v->name, 256, name, 255);
            v->type = dwType;
            v->data = (uint8_t*)malloc(cbData ? cbData : 1);
            if (v->data) {
                if (cbData) memcpy(v->data, lpData, cbData);
                v->data_size = cbData;
            }
            key->value_count++;
            result = ERROR_SUCCESS;
        }
        LeaveCriticalSection(&g_vreg_lock);
        if (result == ERROR_SUCCESS) VReg_SaveSidecar();
        return result;
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

/* ---- 删除钩子（B-3）：存储即真相——删除立即整库落盘；删 sidecar 重置才会
 * 让预置值回来。键删除采用墓碑槽位（path[0]=0）：数组索引保持稳定，已分配
 * 句柄不失效，后续查询/写入按 ERROR_KEY_DELETED 语义拒绝。 ---- */

static LONG WINAPI Hook_RegDeleteValueA(HKEY hKey, LPCSTR lpValueName)
{
    VREG_HANDLE* vh = VReg_GetHandle(hKey);
    if (vh && vh->is_virtual && vh->key_index < g_vreg_key_count) {
        VREG_KEY* key = &g_vreg_keys[vh->key_index];
        const char* name = lpValueName ? lpValueName : "";

        EnterCriticalSection(&g_vreg_lock);
        LONG result;
        if (key->path[0] == '\0') {
            /* 墓碑守卫在锁内复查（与并发删除的窗口竞争） */
            LeaveCriticalSection(&g_vreg_lock);
            return ERROR_KEY_DELETED;
        }
        result = ERROR_FILE_NOT_FOUND;
        for (uint32_t i = 0; i < key->value_count; i++) {
            if (strcmp(key->values[i].name, name) == 0) {
                if (key->values[i].data) free(key->values[i].data);
                /* 保持枚举顺序：整体前移 */
                memmove(&key->values[i], &key->values[i + 1],
                        (key->value_count - i - 1) * sizeof(VREG_VALUE));
                key->value_count--;
                memset(&key->values[key->value_count], 0, sizeof(VREG_VALUE));
                result = ERROR_SUCCESS;
                break;
            }
        }
        LeaveCriticalSection(&g_vreg_lock);
        if (result == ERROR_SUCCESS) VReg_SaveSidecar();
        return result;
    }
    return g_orig_RegDeleteValueA(hKey, lpValueName);
}

static LONG WINAPI Hook_RegDeleteValueW(HKEY hKey, LPCWSTR lpValueName)
{
    VREG_HANDLE* vh = VReg_GetHandle(hKey);
    if (vh && vh->is_virtual) {
        char narrowName[256];
        if (lpValueName)
            WideCharToMultiByte(CP_ACP, 0, lpValueName, -1, narrowName, 256, NULL, NULL);
        else
            narrowName[0] = '\0';
        return Hook_RegDeleteValueA(hKey, narrowName);
    }
    return g_orig_RegDeleteValueW(hKey, lpValueName);
}

/* 作用域内删除虚拟键的核心（A/W 共用，fullPath 为大写 ANSI 全路径）：
 * 有虚拟子键 → ERROR_ACCESS_DENIED（与真实 API 语义一致）；
 * 不存在 → ERROR_FILE_NOT_FOUND（纯虚拟语义，绝不写真实注册表）。 */
static LONG VReg_DeleteKeyCoreA(const char* fullPath) {
    EnterCriticalSection(&g_vreg_lock);
    int32_t idx = VReg_FindKeyA(fullPath);
    if (idx < 0) {
        LeaveCriticalSection(&g_vreg_lock);
        return ERROR_FILE_NOT_FOUND;
    }
    /* 作用域根保护：无存活祖先键的键是作用域锚——删除它会使整个子树退出
     * 虚拟化（后续 Create/Write 透传真实注册表），故拒绝（语义与真实 API
     * 的「有子键不可删」同族）。中段键（有存活祖先）仍可删，祖先继续锚定。 */
    {
        BOOL hasLiveAncestor = FALSE;
        for (uint32_t i = 0; i < g_vreg_key_count; i++) {
            const char* p = g_vreg_keys[i].path;
            if (p[0] == '\0') continue;
            size_t plen = strlen(p);
            if (strncmp(fullPath, p, plen) == 0 && fullPath[plen] == '\\') {
                hasLiveAncestor = TRUE;
                break;
            }
        }
        if (!hasLiveAncestor) {
            LeaveCriticalSection(&g_vreg_lock);
            return ERROR_ACCESS_DENIED;
        }
    }
    size_t len = strlen(fullPath);
    for (uint32_t i = 0; i < g_vreg_key_count; i++) {
        const char* p = g_vreg_keys[i].path;
        if (i != (uint32_t)idx && strncmp(p, fullPath, len) == 0 &&
            p[len] == '\\') {
            LeaveCriticalSection(&g_vreg_lock);
            return ERROR_ACCESS_DENIED;
        }
    }
    VREG_KEY* key = &g_vreg_keys[idx];
    for (uint32_t v = 0; v < key->value_count; v++) {
        if (key->values[v].data) free(key->values[v].data);
    }
    free(key->values);
    /* 墓碑槽位：路径置空 + 释放值数组；索引不变，已开句柄按已删语义拒绝 */
    memset(key, 0, sizeof(*key));
    LeaveCriticalSection(&g_vreg_lock);
    VReg_SaveSidecar();
    return ERROR_SUCCESS;
}

static LONG WINAPI Hook_RegDeleteKeyA(HKEY hKey, LPCSTR lpSubKey)
{
    if (lpSubKey) {
        char fullPath[512];
        BuildKeyPathA(fullPath, 512, hKey, lpSubKey);
        if (VReg_IsInScopeA(fullPath))
            return VReg_DeleteKeyCoreA(fullPath);
    }
    return g_orig_RegDeleteKeyA(hKey, lpSubKey);
}

static LONG WINAPI Hook_RegDeleteKeyW(HKEY hKey, LPCWSTR lpSubKey)
{
    if (lpSubKey) {
        wchar_t fullPathW[512];
        BuildKeyPathW(fullPathW, 512, hKey, lpSubKey);
        char fullPath[512] = {0};
        WideCharToMultiByte(CP_ACP, 0, fullPathW, -1, fullPath, 512, NULL, NULL);
        if (VReg_IsInScopeA(fullPath))
            return VReg_DeleteKeyCoreA(fullPath);
    }
    return g_orig_RegDeleteKeyW(hKey, lpSubKey);
}

/* RegDeleteTree 语义：删子键与值，键本身保留。树删核心（fullPath 存活键）：
 * 全部后代墓碑化 + 本键值清空，一次落盘。 */
static LONG VReg_DeleteTreeCoreA(const char* fullPath) {
    EnterCriticalSection(&g_vreg_lock);
    int32_t idx = VReg_FindKeyA(fullPath);
    if (idx < 0) {
        LeaveCriticalSection(&g_vreg_lock);
        return ERROR_FILE_NOT_FOUND;
    }
    size_t len = strlen(fullPath);
    for (uint32_t i = 0; i < g_vreg_key_count; i++) {
        VREG_KEY* k = &g_vreg_keys[i];
        if (k->path[0] == '\0') continue;
        if (strncmp(k->path, fullPath, len) == 0 && k->path[len] == '\\') {
            for (uint32_t v = 0; v < k->value_count; v++) {
                if (k->values[v].data) free(k->values[v].data);
            }
            free(k->values);
            memset(k, 0, sizeof(*k));
        }
    }
    VREG_KEY* key = &g_vreg_keys[idx];
    for (uint32_t v = 0; v < key->value_count; v++) {
        if (key->values[v].data) free(key->values[v].data);
    }
    free(key->values);
    key->values = NULL;
    key->value_count = 0;
    key->value_capacity = 0;
    LeaveCriticalSection(&g_vreg_lock);
    VReg_SaveSidecar();
    return ERROR_SUCCESS;
}

static LONG WINAPI Hook_RegDeleteTreeA(HKEY hKey, LPCSTR lpSubKey)
{
    char fullPath[512];
    BuildKeyPathA(fullPath, 512, hKey, lpSubKey);
    if (VReg_IsInScopeA(fullPath))
        return VReg_DeleteTreeCoreA(fullPath);
    return g_orig_RegDeleteTreeA(hKey, lpSubKey);
}

static LONG WINAPI Hook_RegDeleteTreeW(HKEY hKey, LPCWSTR lpSubKey)
{
    wchar_t fullPathW[512];
    BuildKeyPathW(fullPathW, 512, hKey, lpSubKey);
    char fullPath[512] = {0};
    WideCharToMultiByte(CP_ACP, 0, fullPathW, -1, fullPath, 512, NULL, NULL);
    if (VReg_IsInScopeA(fullPath))
        return VReg_DeleteTreeCoreA(fullPath);
    return g_orig_RegDeleteTreeW(hKey, lpSubKey);
}

static LONG WINAPI Hook_RegDeleteKeyExA(HKEY hKey, LPCSTR lpSubKey, REGSAM samDesired,
                                        DWORD Reserved)
{
    if (lpSubKey) {
        char fullPath[512];
        BuildKeyPathA(fullPath, 512, hKey, lpSubKey);
        if (VReg_IsInScopeA(fullPath))
            return VReg_DeleteKeyCoreA(fullPath);
    }
    return g_orig_RegDeleteKeyExA(hKey, lpSubKey, samDesired, Reserved);
}

static LONG WINAPI Hook_RegDeleteKeyExW(HKEY hKey, LPCWSTR lpSubKey, REGSAM samDesired,
                                        DWORD Reserved)
{
    if (lpSubKey) {
        wchar_t fullPathW[512];
        BuildKeyPathW(fullPathW, 512, hKey, lpSubKey);
        char fullPath[512] = {0};
        WideCharToMultiByte(CP_ACP, 0, fullPathW, -1, fullPath, 512, NULL, NULL);
        if (VReg_IsInScopeA(fullPath))
            return VReg_DeleteKeyCoreA(fullPath);
    }
    return g_orig_RegDeleteKeyExW(hKey, lpSubKey, samDesired, Reserved);
}

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
    if (MH_CreateHook(&RegCreateKeyExA, &Hook_RegCreateKeyExA,
        (void**)&g_orig_RegCreateKeyExA) != MH_OK) return -10;
    if (MH_CreateHook(&RegCreateKeyExW, &Hook_RegCreateKeyExW,
        (void**)&g_orig_RegCreateKeyExW) != MH_OK) return -11;
    if (MH_CreateHook(&RegDeleteValueA, &Hook_RegDeleteValueA,
        (void**)&g_orig_RegDeleteValueA) != MH_OK) return -12;
    if (MH_CreateHook(&RegDeleteValueW, &Hook_RegDeleteValueW,
        (void**)&g_orig_RegDeleteValueW) != MH_OK) return -13;
    if (MH_CreateHook(&RegDeleteKeyA, &Hook_RegDeleteKeyA,
        (void**)&g_orig_RegDeleteKeyA) != MH_OK) return -14;
    if (MH_CreateHook(&RegDeleteKeyW, &Hook_RegDeleteKeyW,
        (void**)&g_orig_RegDeleteKeyW) != MH_OK) return -15;
    if (MH_CreateHook(&RegDeleteTreeA, &Hook_RegDeleteTreeA,
        (void**)&g_orig_RegDeleteTreeA) != MH_OK) return -16;
    if (MH_CreateHook(&RegDeleteTreeW, &Hook_RegDeleteTreeW,
        (void**)&g_orig_RegDeleteTreeW) != MH_OK) return -17;
    if (MH_CreateHook(&RegDeleteKeyExA, &Hook_RegDeleteKeyExA,
        (void**)&g_orig_RegDeleteKeyExA) != MH_OK) return -18;
    if (MH_CreateHook(&RegDeleteKeyExW, &Hook_RegDeleteKeyExW,
        (void**)&g_orig_RegDeleteKeyExW) != MH_OK) return -19;

    return 0;
}


