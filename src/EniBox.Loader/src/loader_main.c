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
            /*
             * Section layout (set by PeTool's PE_ModBuildEntryPointStub):
             *   x64: [stub:14][VA_placeholder:8][original_ep_rva:4][section_rva:4][VFS data...]
             *   x86: [stub:6][VA_placeholder:4][original_ep_rva:4][section_rva:4][VFS data...]
             *
             * We need to:
             *   1. Read original_ep_rva and section_rva from the metadata
             *   2. Compute ImageBase from the section's known VA
             *   3. Patch the VA placeholder with ImageBase + original_ep_rva
             *   4. Initialize VFS from the data after metadata
             */

            /* Detect architecture from the PE header */
            BOOL is_64bit = FALSE;
            HMODULE hMain = GetModuleHandleW(NULL);
            if (hMain) {
                uint8_t* base = (uint8_t*)hMain;
                IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
                if (dos->e_magic == 0x5A4D) {
                    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
                    if (nt->Signature == 0x00004550) {
                        is_64bit = (nt->FileHeader.Machine == 0x8664); /* IMAGE_FILE_MACHINE_AMD64 */
                    }
                }
            }

            /* Calculate offsets based on architecture */
            uint32_t stub_code_size, va_placeholder_offset, metadata_offset, vfs_data_offset;
            if (is_64bit) {
                /* x64: sub rsp,0x28 (4) + add rsp,0x28 (4) + jmp [rip+0] (6) = 14 bytes code */
                stub_code_size = 14;
                va_placeholder_offset = 14;  /* 8-byte VA placeholder starts at offset 14 */
                metadata_offset = 22;        /* original_ep_rva at offset 22 */
            } else {
                /* x86: push imm32 (5) + ret (1) = 6 bytes code */
                stub_code_size = 6;
                va_placeholder_offset = 1;   /* 4-byte VA placeholder at offset 1 (inside push) */
                metadata_offset = 6;         /* original_ep_rva at offset 6 */
            }
            vfs_data_offset = metadata_offset + 8; /* 4 bytes ep_rva + 4 bytes section_rva */

            /* Read metadata */
            if (section_size >= vfs_data_offset + 4) {
                uint32_t original_ep_rva = *(uint32_t*)(section_base + metadata_offset);
                uint32_t section_rva = *(uint32_t*)(section_base + metadata_offset + 4);
                g_original_entry_rva = original_ep_rva;

                /* Compute ImageBase: section_base = ImageBase + section_rva */
                uintptr_t image_base = (uintptr_t)section_base - section_rva;
                uintptr_t original_ep_va = image_base + original_ep_rva;

                /* Patch the VA placeholder in the stub */
                if (is_64bit) {
                    /* x64: write 8-byte absolute VA at offset 14 */
                    if (section_size >= va_placeholder_offset + 8) {
                        *(uint64_t*)(section_base + va_placeholder_offset) = (uint64_t)original_ep_va;
                    }
                } else {
                    /* x86: write 4-byte absolute VA at offset 1 (inside push imm32) */
                    if (section_size >= va_placeholder_offset + 4) {
                        *(uint32_t*)(section_base + va_placeholder_offset) = (uint32_t)original_ep_va;
                    }
                }

                /* Initialize VFS from data after metadata */
                if (section_size > vfs_data_offset) {
                    Loader_Initialize(section_base + vfs_data_offset, section_size - vfs_data_offset);
                }
            }
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
