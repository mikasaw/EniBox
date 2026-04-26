#include "loader_main.h"
#include "vfs_runtime.h"
#include "hook_manager.h"
#include "hook_process.h"
#include <windows.h>

static uint8_t* g_vfs_base = NULL;
static uint32_t g_vfs_size = 0;
static BOOL g_initialized = FALSE;
static uint32_t g_original_entry_rva = 0;
static wchar_t g_loader_path[MAX_PATH] = {0};

static uint8_t* FindEniboxSection(uint32_t* section_size) {
    HMODULE hModule = GetModuleHandleW(NULL);
    if (!hModule) return NULL;
    uint8_t* base = (uint8_t*)hModule;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != 0x5A4D) return NULL;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    if (nt->Signature != 0x00004550) return NULL;
    IMAGE_SECTION_HEADER* sections = (IMAGE_SECTION_HEADER*)((uint8_t*)nt +
        sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) + nt->FileHeader.SizeOfOptionalHeader);
    for (uint16_t i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (memcmp(sections[i].Name, ".enibox", 8) == 0) {
            *section_size = sections[i].SizeOfRawData;
            return base + sections[i].VirtualAddress;
        }
    }
    return NULL;
}

int32_t Loader_Initialize(uint8_t* vfs_base, uint32_t vfs_size) {
    if (g_initialized) return 0;
    int32_t result = VFS_Initialize(vfs_base, vfs_size);
    if (result != 0) return result;
    result = Hook_Initialize();
    if (result != 0) { VFS_Finalize(); return result; }
    result = Hook_InstallFileHooks();
    if (result != 0) { Hook_Uninitialize(); VFS_Finalize(); return result; }
    Hook_InstallProcessHooks();
    Hook_InstallRegistryHooks();
    result = Hook_EnableAll();
    if (result != 0) { Hook_Uninitialize(); VFS_Finalize(); return result; }
    g_vfs_base = vfs_base;
    g_vfs_size = vfs_size;
    g_initialized = TRUE;
    return 0;
}

void Loader_Finalize(void) {
    if (!g_initialized) return;
    Hook_DisableAll();
    Hook_Uninitialize();
    VFS_Finalize();
    g_initialized = FALSE;
    g_vfs_base = NULL;
    g_vfs_size = 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: {
        /* Store the Loader DLL path for child process injection */
        GetModuleFileNameW(hModule, g_loader_path, MAX_PATH);
        HookProcess_SetLoaderPath(g_loader_path);

        uint32_t section_size = 0;
        uint8_t* section_base = FindEniboxSection(&section_size);
        if (section_base) {
            g_original_entry_rva = *(uint32_t*)section_base;
            Loader_Initialize(section_base + 8, section_size - 8);
        }
        DisableThreadLibraryCalls(hModule);
        break;
    }
    case DLL_PROCESS_DETACH:
        Loader_Finalize();
        break;
    }
    return TRUE;
}
