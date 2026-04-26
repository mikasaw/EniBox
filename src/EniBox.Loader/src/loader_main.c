#include "loader_main.h"
#include "vfs_runtime.h"
#include "hook_manager.h"
#include "hook_process.h"
#include <windows.h>
#include <stdio.h>

static uint8_t* g_vfs_base = NULL;
static uint32_t g_vfs_size = 0;
static BOOL g_initialized = FALSE;
static uint32_t g_original_entry_rva = 0;
static wchar_t g_loader_path[MAX_PATH] = {0};
static wchar_t g_extracted_loader_path[MAX_PATH] = {0};
static BOOL g_loader_extracted = FALSE;

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

/*
 * ExtractEmbeddedLoader - Extract the embedded Loader DLL from the .enibox
 * section to a temporary file. This allows the Loader to be loaded via the
 * import table (which references "EniBox.Loader.dll" as an external file)
 * while the actual DLL bytes are embedded in the section.
 *
 * The extracted file is written to %TEMP%\EniBox.Loader.<pid>.dll to avoid
 * conflicts between multiple packed EXEs running simultaneously.
 *
 * Returns: TRUE if extraction succeeded, FALSE otherwise.
 */
static BOOL ExtractEmbeddedLoader(const uint8_t* loader_data, uint32_t loader_size) {
    if (!loader_data || loader_size == 0) return FALSE;

    /* Validate it's a PE DLL (MZ header) */
    if (loader_data[0] != 'M' || loader_data[1] != 'Z') return FALSE;

    /* Generate temp file path: %TEMP%\EniBox.Loader.<pid>.dll */
    wchar_t temp_dir[MAX_PATH];
    DWORD temp_len = GetTempPathW(MAX_PATH, temp_dir);
    if (temp_len == 0 || temp_len >= MAX_PATH) return FALSE;

    DWORD pid = GetCurrentProcessId();
    _snwprintf_s(g_extracted_loader_path, MAX_PATH, _TRUNCATE,
                 L"%sEniBox.Loader.%u.dll", temp_dir, pid);

    /* Write the DLL to the temp file */
    HANDLE hFile = CreateFileW(g_extracted_loader_path,
        GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    DWORD bytes_written;
    BOOL success = WriteFile(hFile, loader_data, loader_size, &bytes_written, NULL);
    CloseHandle(hFile);

    if (!success || bytes_written != loader_size) {
        DeleteFileW(g_extracted_loader_path);
        g_extracted_loader_path[0] = 0;
        return FALSE;
    }

    g_loader_extracted = TRUE;
    return TRUE;
}

/*
 * CleanupExtractedLoader - Delete the temporarily extracted Loader DLL.
 * Called during DLL_PROCESS_DETACH.
 */
static void CleanupExtractedLoader(void) {
    if (g_loader_extracted && g_extracted_loader_path[0] != 0) {
        /* Delay deletion - the DLL may still be in use.
         * MoveFileEx with MOVEFILE_DELAY_UNTIL_REBOOT ensures cleanup
         * even if we can't delete it now. */
        MoveFileExW(g_extracted_loader_path, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
        DeleteFileW(g_extracted_loader_path);
        g_extracted_loader_path[0] = 0;
        g_loader_extracted = FALSE;
    }
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
             * Section layout (set by PackService.CombineSectionData + PeTool stub):
             *
             * x64: [stub:14][VA_placeholder:8][original_ep_rva:4][section_rva:4]
             *      [vfs_total_size:4][VFS data...][Loader DLL bytes...]
             *
             * x86: [stub:6][VA_placeholder:4][original_ep_rva:4][section_rva:4]
             *      [vfs_total_size:4][VFS data...][Loader DLL bytes...]
             *
             * The vfs_total_size field tells us exactly how many bytes of VFS
             * data follow, so we can correctly split VFS from the embedded
             * Loader DLL bytes.
             *
             * Steps:
             *   1. Read original_ep_rva and section_rva from metadata
             *   2. Compute ImageBase from the section's known VA
             *   3. Patch the VA placeholder with ImageBase + original_ep_rva
             *   4. Read vfs_total_size to determine VFS data boundary
             *   5. Initialize VFS from the VFS data region
             *   6. Extract embedded Loader DLL to temp file for child process injection
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
            /* After metadata (8 bytes): vfs_total_size (4 bytes), then VFS data */
            vfs_data_offset = metadata_offset + 8 + 4; /* ep_rva(4) + section_rva(4) + vfs_size(4) */

            /* Read metadata */
            if (section_size >= vfs_data_offset) {
                uint32_t original_ep_rva = *(uint32_t*)(section_base + metadata_offset);
                uint32_t section_rva = *(uint32_t*)(section_base + metadata_offset + 4);
                uint32_t vfs_total_size = *(uint32_t*)(section_base + metadata_offset + 8);
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

                /* Initialize VFS from the VFS data region */
                uint32_t vfs_end_offset = vfs_data_offset + vfs_total_size;
                if (vfs_total_size > 0 && vfs_end_offset <= section_size) {
                    Loader_Initialize(section_base + vfs_data_offset, vfs_total_size);
                }

                /* Extract embedded Loader DLL for child process injection */
                if (vfs_end_offset < section_size) {
                    uint32_t loader_size = section_size - vfs_end_offset;
                    uint8_t* loader_data = section_base + vfs_end_offset;
                    if (ExtractEmbeddedLoader(loader_data, loader_size)) {
                        /* Use the extracted path for child process injection
                         * instead of the currently loaded DLL's path.
                         * This ensures child processes get the correct Loader
                         * even if the import table points to a different location. */
                        HookProcess_SetLoaderPath(g_extracted_loader_path);
                    }
                }
            }
        }
        DisableThreadLibraryCalls(hModule);
        break;
    }
    case DLL_PROCESS_DETACH:
        CleanupExtractedLoader();
        Loader_Finalize();
        break;
    }
    return TRUE;
}
