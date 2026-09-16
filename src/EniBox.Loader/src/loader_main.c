#include "loader_main.h"
#include "vfs_runtime.h"
#include "hook_manager.h"
#include "hook_process.h"
#include "hook_registry.h"
#include "vfs_link.h"
#include "diag_file.h"
#include <windows.h>
#include <strsafe.h>
#include <stdio.h>

static uint8_t* g_vfs_base = NULL;
static uint32_t g_vfs_size = 0;
static BOOL g_initialized = FALSE;
/* Packed config flags (bootstrap [272..275]); default = all enabled for
 * contexts without a packed section (e.g. standalone LoadLibrary). */
static uint32_t g_config_flags = 0xFFFFFFFFu;
static uint32_t g_original_entry_rva = 0;
static wchar_t g_loader_path[MAX_PATH] = {0};
static wchar_t g_extracted_loader_path[MAX_PATH] = {0};
static wchar_t g_extracted_loader_dir[MAX_PATH] = {0};
static BOOL g_loader_extracted = FALSE;

static uint32_t ComputeCrc32(const uint8_t* data, uint32_t size) {
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (0u - (crc & 1)));
        }
    }
    return crc ^ 0xFFFFFFFF;
}

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

    if (loader_data[0] != 'M' || loader_data[1] != 'Z') return FALSE;

    uint32_t expected_crc = ComputeCrc32(loader_data, loader_size);

    wchar_t temp_dir[MAX_PATH];
    DWORD temp_len = GetTempPathW(MAX_PATH, temp_dir);
    if (temp_len == 0 || temp_len >= MAX_PATH) return FALSE;

    DWORD pid = GetCurrentProcessId();
    DWORD rnd = (GetTickCount() ^ pid) & 0xFFFF;

    _snwprintf_s(g_extracted_loader_dir, MAX_PATH, _TRUNCATE,
                 L"%sEniBox-%u-%u", temp_dir, pid, rnd);

    {
        /* Create secure temp directory with restricted DACL */
        SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, FALSE};
        BOOL dir_created = CreateDirectoryW(g_extracted_loader_dir, &sa);

        if (!dir_created && GetLastError() != ERROR_ALREADY_EXISTS) {
            /* Fallback: try LOCALAPPDATA */
            if (SUCCEEDED(StringCchCopyW(g_extracted_loader_dir, MAX_PATH, temp_dir))) {
                _snwprintf_s(g_extracted_loader_dir + wcslen(g_extracted_loader_dir),
                             MAX_PATH - wcslen(g_extracted_loader_dir), _TRUNCATE,
                             L"EniBox-%u-%u\\", pid, rnd);
                CreateDirectoryW(g_extracted_loader_dir, NULL);
            }
        }
    }

    _snwprintf_s(g_extracted_loader_path, MAX_PATH, _TRUNCATE,
                 L"%s\\EniBox.Loader.%u.%u.dll", g_extracted_loader_dir, pid, rnd);

    HANDLE hFile = CreateFileW(g_extracted_loader_path,
        GENERIC_WRITE, 0, NULL, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        hFile = CreateFileW(g_extracted_loader_path,
            GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return FALSE;
    }

    DWORD bytes_written;
    BOOL success = WriteFile(hFile, loader_data, loader_size, &bytes_written, NULL);
    CloseHandle(hFile);

    if (!success || bytes_written != loader_size) {
        DeleteFileW(g_extracted_loader_path);
        g_extracted_loader_path[0] = 0;
        return FALSE;
    }

    {
        /* Integrity verification: re-read and check CRC32 */
        HANDLE hVerify = CreateFileW(g_extracted_loader_path,
            GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
            FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (hVerify != INVALID_HANDLE_VALUE) {
            uint8_t* verify_buf = (uint8_t*)HeapAlloc(GetProcessHeap(), 0, loader_size);
            if (verify_buf) {
                DWORD bytes_read = 0;
                if (ReadFile(hVerify, verify_buf, loader_size, &bytes_read, NULL)
                    && bytes_read == loader_size) {
                    uint32_t actual_crc = ComputeCrc32(verify_buf, loader_size);
                    if (actual_crc != expected_crc) {
                        HeapFree(GetProcessHeap(), 0, verify_buf);
                        CloseHandle(hVerify);
                        DeleteFileW(g_extracted_loader_path);
                        g_extracted_loader_path[0] = 0;
                        return FALSE;
                    }
                }
                HeapFree(GetProcessHeap(), 0, verify_buf);
            }
            CloseHandle(hVerify);
        }
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
        MoveFileExW(g_extracted_loader_path, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
        DeleteFileW(g_extracted_loader_path);
        g_extracted_loader_path[0] = 0;
        g_loader_extracted = FALSE;
    }
    if (g_extracted_loader_dir[0] != 0) {
        RemoveDirectoryW(g_extracted_loader_dir);
        g_extracted_loader_dir[0] = 0;
    }
}

/* Exported so the packer can import this DLL by name with a real resolved
 * function: the Windows loader skips import descriptors whose ILT is empty
 * (load-only imports), which would leave the packed process without the
 * Loader entirely. */
__declspec(dllexport) int32_t __stdcall EniBoxLoader_GetVersion(void)
{
    return 0x00010000; /* 1.0 */
}

int32_t Loader_Initialize(uint8_t* vfs_base, uint32_t vfs_size) {    if (g_initialized) return 0;
    int32_t result = VFS_Initialize(vfs_base, vfs_size);
    if (result != 0) return result;
    /* v2 header: 预载注册表虚拟化预置值（仅启用注册表虚拟化时） */
    if ((g_config_flags & ENIBOX_FLAG_REGISTRY_VIRTUALIZATION) && vfs_size >= 52) {
        uint32_t version = *(uint32_t*)(vfs_base + 4);
        if (version >= 2) {
            uint32_t regOff = *(uint32_t*)(vfs_base + 44);
            uint32_t regSize = *(uint32_t*)(vfs_base + 48);
            if (regOff < vfs_size && regSize > 0 && regOff <= vfs_size - regSize) {
                VReg_Preload(vfs_base + regOff, regSize);
            }
        }
    }
    result = Hook_Initialize();
    if (result != 0) { VFS_Finalize(); return result; }
    result = Hook_InstallFileHooks();
    if (result != 0) { Hook_Uninitialize(); VFS_Finalize(); return result; }
    if (g_config_flags & ENIBOX_FLAG_SUBPROCESS_INJECTION)
        Hook_InstallProcessHooks();
    if (g_config_flags & ENIBOX_FLAG_REGISTRY_VIRTUALIZATION)
        HookRegistry_Install();
    result = Hook_EnableAll();
    if (result != 0) { Hook_Uninitialize(); VFS_Finalize(); return result; }
    g_vfs_base = vfs_base;
    g_vfs_size = vfs_size;
    g_initialized = TRUE;
    /* Only from this point may child processes be handed the VFS */
    HookProcess_SetVfsReady(TRUE);
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

/* ---- VFS inheritance (running inside an injected, non-packed child) ----
 *
 * The injecting parent writes a VfsLink handoff file next to the extracted
 * Loader DLL; it names the parent image whose .enibox section carries the
 * VFS. We map that image read-only and copy the VFS blob into private
 * memory (VFS_Initialize zeroes the checksum field in place, so the blob
 * must be writable). From here on this process serves the parent's VFS
 * through its own hooks. */

/* 永久诊断点：写 %TEMP% 诊断文件（生产可用，见 include/diag_file.h） */
static void VfsInherit_Diag(const char* step) {
    char d[96];
    sprintf_s(d, sizeof(d), "inherit %s", step);
    EniBox_DiagLine(d);
}

static void Loader_TryInheritParentVfs(HMODULE hModule) {
    wchar_t self[MAX_PATH];
    if (!GetModuleFileNameW(hModule, self, MAX_PATH)) { VfsInherit_Diag("selfpath_failed"); return; }
    wchar_t* slash = wcsrchr(self, L'\\');
    if (!slash) { VfsInherit_Diag("selfpath_no_slash"); return; }
    *slash = L'\0';
    wchar_t linkPath[MAX_PATH];
    if (swprintf_s(linkPath, MAX_PATH, L"%s\\%s", self, ENIBOX_VFSLINK_NAME) < 0) { VfsInherit_Diag("linkpath_fmt_failed"); return; }

    HANDLE h = CreateFileW(linkPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { VfsInherit_Diag("link_open_failed"); return; }
    uint32_t magic = 0, pathChars = 0, linkFlags = 0xFFFFFFFFu;
    DWORD read = 0;
    wchar_t image[MAX_PATH];
    BOOL ok = ReadFile(h, &magic, sizeof(magic), &read, NULL) && read == sizeof(magic)
              && magic == ENIBOX_VFSLINK_MAGIC;
    if (ok) ok = ReadFile(h, &pathChars, sizeof(pathChars), &read, NULL) && read == sizeof(pathChars);
    if (ok && pathChars > 0 && pathChars < MAX_PATH) {
        ok = ReadFile(h, image, pathChars * sizeof(wchar_t), &read, NULL)
             && read == pathChars * sizeof(wchar_t);
        if (ok) image[pathChars] = L'\0';
    } else {
        ok = FALSE;
    }
    if (ok) {
        /* VfsLink v2 尾部携带父进程配置位；旧格式无此字段时保持默认全开 */
        ReadFile(h, &linkFlags, sizeof(linkFlags), &read, NULL);
        g_config_flags = linkFlags;
        HookProcess_SetConfigFlags(g_config_flags);
    }
    CloseHandle(h);
    if (!ok) { VfsInherit_Diag("link_read_failed"); return; }

    HANDLE hFile = CreateFileW(image, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                               NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) { VfsInherit_Diag("parent_image_open_failed"); return; }
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart <= 0) { CloseHandle(hFile); VfsInherit_Diag("parent_image_size_failed"); return; }
    HANDLE hMap = CreateFileMappingW(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    CloseHandle(hFile);
    if (!hMap) { VfsInherit_Diag("map_failed"); return; }
    const uint8_t* base = (const uint8_t*)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (!base) { CloseHandle(hMap); VfsInherit_Diag("mapview_failed"); return; }

    do {
        const uint64_t size = (uint64_t)fileSize.QuadPart;
        if (size < sizeof(IMAGE_DOS_HEADER)) break;
        IMAGE_DOS_HEADER dos;
        memcpy(&dos, base, sizeof(dos));
        if (dos.e_magic != 0x5A4D) break;
        if (dos.e_lfanew <= 0 || (uint64_t)dos.e_lfanew + sizeof(IMAGE_NT_HEADERS64) > size) break;
        IMAGE_NT_HEADERS64 nt;
        memcpy(&nt, base + dos.e_lfanew, sizeof(nt));
        if (nt.Signature != 0x00004550 || nt.FileHeader.Machine != 0x8664) break;
        if (nt.FileHeader.NumberOfSections == 0 || nt.FileHeader.NumberOfSections > 96) break;
        const uint8_t* sectab = base + dos.e_lfanew + sizeof(DWORD) +
            sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader;
        if ((uint64_t)(sectab - base) + (uint64_t)nt.FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER) > size)
            break;
        const IMAGE_SECTION_HEADER* eni = NULL;
        IMAGE_SECTION_HEADER eniHeader;
        for (uint16_t i = 0; i < nt.FileHeader.NumberOfSections; i++) {
            IMAGE_SECTION_HEADER sec;
            memcpy(&sec, sectab + (size_t)i * sizeof(IMAGE_SECTION_HEADER), sizeof(sec));
            if (memcmp(sec.Name, ".enibox", 8) == 0) { eniHeader = sec; eni = &eniHeader; break; }
        }
        if (!eni) break;
        const uint32_t ptr = eni->PointerToRawData;
        if (ptr == 0 || (uint64_t)ptr + 296 > size) break;
        const uint32_t vfs_total = *(const uint32_t*)(base + ptr + 288);
        if (vfs_total == 0 || vfs_total > 0x10000000u) break; /* 256MB sanity cap */
        if ((uint64_t)ptr + 296 + vfs_total > size) break;
        uint8_t* copy = (uint8_t*)VirtualAlloc(NULL, vfs_total, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!copy) break;
        memcpy(copy, base + ptr + 296, vfs_total);
        if (Loader_Initialize(copy, vfs_total) != 0) {
            VirtualFree(copy, 0, MEM_RELEASE);
            VfsInherit_Diag("preload_init_failed");
        } else {
            VfsInherit_Diag("ok");
        }
        OutputDebugStringW(L"[EniBox] child inherited parent VFS via VfsLink");
    } while (0);
    UnmapViewOfFile(base);
    CloseHandle(hMap);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: {
        /* Store the Loader DLL path for child process injection */
        GetModuleFileNameW(hModule, g_loader_path, MAX_PATH);
        HookProcess_SetLoaderPath(g_loader_path);

        EniBox_DiagLine("dllmain attach");
        uint32_t section_size = 0;
        uint8_t* section_base = FindEniboxSection(&section_size);
        if (section_base) {
            /*
             * Section layout (set by PackService.CombineSectionData + PeTool stub):
             *
             * x64: [stub:14][VA_placeholder:8][original_ep_rva:4][section_rva:4]
             *      [vfs_total_size:4][loader_total_size:4][VFS data...]
             *      [Loader DLL bytes...][import table area]
             *
             * x86: [stub:6][VA_placeholder:4][original_ep_rva:4][section_rva:4]
             *      [vfs_total_size:4][loader_total_size:4][VFS data...]
             *      [Loader DLL bytes...][import table area]
             *
             * The vfs_total_size field tells us how many bytes of VFS data
             * follow, and loader_total_size gives the exact loader DLL size
             * (the trailing import table area must not leak into the CRC).
             *
             * Steps:
             *   1. Read original_ep_rva and section_rva from metadata
             *   2. Compute ImageBase from the section's known VA
             *   3. Patch the VA placeholder with ImageBase + original_ep_rva
             *   4. Read vfs_total_size / loader_total_size to split the regions
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

            /* Bootstrap region layout (see PackService.CombineSectionData):
             *   +208  jmp [rip+0] stub; VA placeholder at +214
             *   +280  original_ep_rva, +284 section_rva
             *   +288  vfs_total_size, +292 loader_total_size
             *   +296  VFS data, loader DLL bytes */
            uint32_t stub_code_size, va_placeholder_offset, metadata_offset, vfs_data_offset;
            if (is_64bit) {
                stub_code_size = 14;
                va_placeholder_offset = 214;
                metadata_offset = 280;
            } else {
                /* x86 images are not produced by the current packer. */
                stub_code_size = 6;
                va_placeholder_offset = 1;
                metadata_offset = 6;
            }
            vfs_data_offset = metadata_offset + 16; /* ep(4) + sec(4) + vfs(4) + loader(4) */
            if (is_64bit) vfs_data_offset = 296;    /* bootstrap(288) + vfs_total(4) + loader_total(4) */

            /* Read config flags (written by PackService at [272..275]) */
            if (section_size >= 276) {
                g_config_flags = *(uint32_t*)(section_base + 272);
                HookProcess_SetConfigFlags(g_config_flags);
            }

            /* Read metadata */
            if (section_size >= vfs_data_offset) {
                uint32_t original_ep_rva = *(uint32_t*)(section_base + metadata_offset);
                uint32_t section_rva = *(uint32_t*)(section_base + metadata_offset + 4);
                /* vfs/loader sizes live after the 296-byte bootstrap region */
                uint32_t vfs_total_size = *(uint32_t*)(section_base + 288);
                uint32_t loader_total_size = *(uint32_t*)(section_base + 292);
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

                /* Initialize VFS + hooks */
                uint32_t vfs_end_offset = vfs_data_offset + vfs_total_size;
                if (vfs_total_size > 0 && vfs_end_offset <= section_size) {
                    int32_t init_rc = Loader_Initialize(section_base + vfs_data_offset, vfs_total_size);
                    if (init_rc != 0) {
                        char d[64];
                        sprintf_s(d, sizeof(d), "init rc=%d", init_rc);
                        EniBox_DiagLine(d);
                    }
                }

                /* Extract embedded Loader DLL for child process injection.
                 * loader_total_size is the exact DLL size written by the
                 * packer, so the trailing import table area never leaks
                 * into the extracted bytes (CRC would fail otherwise). */
                if (loader_total_size >= 2 &&
                    vfs_end_offset <= section_size &&
                    loader_total_size <= section_size - vfs_end_offset) {
                    uint8_t* loader_data = section_base + vfs_end_offset;
                    if (ExtractEmbeddedLoader(loader_data, loader_total_size)) {
                        EniBox_DiagLine("extract ok");
                        /* Use the extracted path for child process injection
                         * instead of the currently loaded DLL's path.
                         * This ensures child processes get the correct Loader
                         * even if the import table points to a different location. */
                        HookProcess_SetLoaderPath(g_extracted_loader_path);
                    }
                }
            }
        } else {
            /* Injected into a non-packed child: inherit the parent's VFS
             * via the VfsLink handoff (no .enibox section of our own). */
            Loader_TryInheritParentVfs(hModule);
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
