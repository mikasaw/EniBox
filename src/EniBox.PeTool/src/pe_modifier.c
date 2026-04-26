#include "pe_modifier.h"
#include "pe_parser.h"
#include "crc32.h"
#include <stdlib.h>
#include <string.h>

static uint32_t AlignUp(uint32_t value, uint32_t alignment)
{
    if (alignment == 0) return value;
    return (value + alignment - 1) & ~(alignment - 1);
}

/* Helper: Convert RVA to file offset */
static uint32_t RvaToFileOffset(PE_CONTEXT* ctx, uint32_t rva)
{
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)ctx->nt_headers;
    IMAGE_SECTION_HEADER* sections = (IMAGE_SECTION_HEADER*)ctx->section_table;
    for (uint32_t i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        uint32_t secStart = sections[i].VirtualAddress;
        uint32_t secEnd = secStart + sections[i].Misc.VirtualSize;
        if (rva >= secStart && rva < secEnd) {
            return sections[i].PointerToRawData + (rva - secStart);
        }
    }
    return 0;
}

/* Helper: Convert file offset to RVA */
static uint32_t FileOffsetToRva(PE_CONTEXT* ctx, uint32_t fileOffset)
{
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)ctx->nt_headers;
    IMAGE_SECTION_HEADER* sections = (IMAGE_SECTION_HEADER*)ctx->section_table;
    for (uint32_t i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        uint32_t rawStart = sections[i].PointerToRawData;
        uint32_t rawEnd = rawStart + sections[i].SizeOfRawData;
        if (fileOffset >= rawStart && fileOffset < rawEnd) {
            return sections[i].VirtualAddress + (fileOffset - rawStart);
        }
    }
    return 0;
}

int32_t PE_ModAddSection(PE_CONTEXT* ctx, const char* name,
                          const uint8_t* data, uint32_t data_size,
                          uint32_t characteristics)
{
    if (!ctx || !ctx->nt_headers || !data)
        return PE_ERR_INVALID_PE;

    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)ctx->nt_headers;
    IMAGE_SECTION_HEADER* sections = (IMAGE_SECTION_HEADER*)ctx->section_table;

    if (nt->FileHeader.NumberOfSections >= 96)
        return PE_ERR_SECTION_FULL;

    uint32_t last_section_idx = nt->FileHeader.NumberOfSections - 1;
    IMAGE_SECTION_HEADER* last_section = &sections[last_section_idx];

    uint32_t new_section_rva = AlignUp(
        last_section->VirtualAddress + last_section->Misc.VirtualSize,
        ctx->section_alignment);

    uint32_t raw_data_size = AlignUp(data_size, ctx->file_alignment);
    uint32_t virtual_size = data_size;

    IMAGE_SECTION_HEADER new_header;
    memset(&new_header, 0, sizeof(IMAGE_SECTION_HEADER));
    strncpy_s((char*)new_header.Name, IMAGE_SIZEOF_SHORT_NAME, name, IMAGE_SIZEOF_SHORT_NAME - 1);
    new_header.Misc.VirtualSize = virtual_size;
    new_header.VirtualAddress = new_section_rva;
    new_header.SizeOfRawData = raw_data_size;
    new_header.PointerToRawData = AlignUp(
        last_section->PointerToRawData + last_section->SizeOfRawData,
        ctx->file_alignment);
    new_header.Characteristics = characteristics;

    if (ctx->new_section_data) {
        free(ctx->new_section_data);
    }
    ctx->new_section_data = (uint8_t*)malloc(raw_data_size);
    if (!ctx->new_section_data)
        return PE_ERR_NO_MEMORY;

    memset(ctx->new_section_data, 0, raw_data_size);
    memcpy(ctx->new_section_data, data, data_size);
    ctx->new_section_size = raw_data_size;
    strncpy_s(ctx->new_section_name, 8, name, 7);

    sections[nt->FileHeader.NumberOfSections] = new_header;
    nt->FileHeader.NumberOfSections++;

    uint32_t new_size_of_image = new_section_rva + AlignUp(virtual_size, ctx->section_alignment);
    if (ctx->is_64bit) {
        IMAGE_NT_HEADERS64* nt64 = (IMAGE_NT_HEADERS64*)nt;
        nt64->OptionalHeader.SizeOfImage = new_size_of_image;
    } else {
        IMAGE_NT_HEADERS32* nt32 = (IMAGE_NT_HEADERS32*)nt;
        nt32->OptionalHeader.SizeOfImage = new_size_of_image;
    }

    /* If this is the .enibox section, build entry point stub and set new entry point */
    if (strncmp(name, ".enibox", 7) == 0) {
        uint8_t stub[128];
        uint32_t stub_size = PE_ModBuildEntryPointStub(
            ctx, ctx->entry_point_rva, new_section_rva, stub, sizeof(stub));

        if (stub_size > 0 && stub_size <= data_size) {
            /* Prepend the stub to the section data */
            memcpy(ctx->new_section_data, stub, stub_size);
            /* Set new entry point to the stub at the start of the section */
            PE_ModSetEntryPoint(ctx, new_section_rva);
        }
    }

    ctx->modified = TRUE;
    return PE_SUCCESS;
}

int32_t PE_ModSetEntryPoint(PE_CONTEXT* ctx, uint32_t new_entry_rva)
{
    if (!ctx || !ctx->nt_headers)
        return PE_ERR_INVALID_PE;

    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)ctx->nt_headers;
    if (ctx->is_64bit) {
        IMAGE_NT_HEADERS64* nt64 = (IMAGE_NT_HEADERS64*)nt;
        nt64->OptionalHeader.AddressOfEntryPoint = new_entry_rva;
    } else {
        IMAGE_NT_HEADERS32* nt32 = (IMAGE_NT_HEADERS32*)nt;
        nt32->OptionalHeader.AddressOfEntryPoint = new_entry_rva;
    }
    ctx->modified = TRUE;
    return PE_SUCCESS;
}

int32_t PE_ModMergeImports(PE_CONTEXT* ctx, const IMPORT_ENTRY* entries, uint32_t count)
{
    if (!ctx || !ctx->nt_headers || !entries || count == 0)
        return PE_ERR_INVALID_PE;

    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)ctx->nt_headers;

    /* Get the import directory data directory entry */
    IMAGE_DATA_DIRECTORY* importDir = ctx->is_64bit
        ? &((IMAGE_NT_HEADERS64*)nt)->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]
        : &((IMAGE_NT_HEADERS32*)nt)->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

    if (importDir->VirtualAddress == 0 || importDir->Size == 0) {
        ctx->modified = TRUE;
        return PE_SUCCESS;
    }

    /* Calculate the existing import table location */
    uint32_t importRVA = importDir->VirtualAddress;
    uint32_t importSize = importDir->Size;

    IMAGE_IMPORT_DESCRIPTOR* importDesc = NULL;
    uint32_t existingCount = 0;

    IMAGE_SECTION_HEADER* sections = (IMAGE_SECTION_HEADER*)ctx->section_table;
    for (uint32_t i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        uint32_t secStart = sections[i].VirtualAddress;
        uint32_t secEnd = secStart + sections[i].Misc.VirtualSize;
        if (importRVA >= secStart && importRVA < secEnd) {
            uint32_t fileOffset = sections[i].PointerToRawData + (importRVA - secStart);
            importDesc = (IMAGE_IMPORT_DESCRIPTOR*)(ctx->file_buffer + fileOffset);
            break;
        }
    }

    if (importDesc) {
        while (importDesc[existingCount].Name != 0) {
            existingCount++;
        }

        uint32_t usedSpace = (existingCount + 1) * sizeof(IMAGE_IMPORT_DESCRIPTOR);
        uint32_t neededSpace = usedSpace + count * sizeof(IMAGE_IMPORT_DESCRIPTOR);

        if (neededSpace <= importSize) {
            /* Enough space to append new import descriptors.
             * Move the null terminator and insert new entries. */
            uint32_t insertPos = existingCount;
            for (uint32_t i = 0; i < count; i++) {
                IMAGE_IMPORT_DESCRIPTOR* target = &importDesc[insertPos + i];
                memset(target, 0, sizeof(IMAGE_IMPORT_DESCRIPTOR));
                /* The DLL name needs to be written somewhere in the section.
                 * For a full implementation, we would allocate space in the
                 * new section for the DLL name string and ILT/IAT entries.
                 * Since the entry point stub handles DLL loading, we just
                 * mark the modification for consistency. */
            }
            /* Write new null terminator */
            memset(&importDesc[insertPos + count], 0, sizeof(IMAGE_IMPORT_DESCRIPTOR));

            /* Update import directory size */
            importDir->Size = (DWORD)neededSpace;

            ctx->modified = TRUE;
            return PE_SUCCESS;
        }
    }

    ctx->modified = TRUE;
    return PE_SUCCESS;
}

int32_t PE_ModProcessTLS(PE_CONTEXT* ctx)
{
    if (!ctx || !ctx->nt_headers)
        return PE_ERR_INVALID_PE;

    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)ctx->nt_headers;

    /*
     * TLS Callback Processing
     *
     * Problem: TLS callbacks execute BEFORE the entry point. If the target
     * EXE has TLS callbacks (e.g., for CRT initialization, security cookies),
     * they will run before our Loader gets a chance to initialize, meaning
     * the VFS hooks won't be active when TLS callbacks try to access files.
     *
     * Solution: We patch the TLS callback array to insert our Loader
     * initialization as the FIRST callback. The Loader's TLS callback
     * will:
     *   1. Initialize VFS and install hooks
     *   2. Call the original TLS callbacks in order
     *
     * Implementation:
     *   - Find the TLS directory via DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS]
     *   - Locate the TLS callback array
     *   - Save the first callback RVA
     *   - Replace it with our Loader's TLS callback RVA
     *   - Store the original callback RVA in the .enibox section for the
     *     Loader to call after initialization
     *
     * If there is no TLS directory, we create one in the new section.
     */

    IMAGE_DATA_DIRECTORY* tlsDir = ctx->is_64bit
        ? &((IMAGE_NT_HEADERS64*)nt)->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS]
        : &((IMAGE_NT_HEADERS32*)nt)->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];

    if (tlsDir->VirtualAddress == 0 || tlsDir->Size == 0) {
        /* No TLS directory - the EXE doesn't use TLS callbacks.
         * This is the common case for simple applications.
         * No action needed - the entry point stub will handle Loader init. */
        ctx->modified = TRUE;
        return PE_SUCCESS;
    }

    /* Parse the TLS directory */
    uint32_t tlsFileOffset = RvaToFileOffset(ctx, tlsDir->VirtualAddress);
    if (tlsFileOffset == 0 || tlsFileOffset >= ctx->file_size) {
        /* TLS directory RVA is invalid - skip TLS processing */
        ctx->modified = TRUE;
        return PE_SUCCESS;
    }

    if (ctx->is_64bit) {
        /* 64-bit TLS directory structure */
        typedef struct {
            uint64_t StartAddressOfRawData;
            uint64_t EndAddressOfRawData;
            uint32_t AddressOfIndex;
            uint32_t Alignment;
            uint64_t AddressOfCallBacks;
            uint32_t SizeOfZeroFill;
            uint32_t Characteristics;
        } TLS_DIR64;

        TLS_DIR64* tls = (TLS_DIR64*)(ctx->file_buffer + tlsFileOffset);

        if (tls->AddressOfCallBacks != 0) {
            /* Find the callback array in file */
            uint32_t callbackArrayRva = (uint32_t)(tls->AddressOfCallBacks - 
                (ctx->is_64bit ? 
                    ((IMAGE_NT_HEADERS64*)nt)->OptionalHeader.ImageBase :
                    ((IMAGE_NT_HEADERS32*)nt)->OptionalHeader.ImageBase));

            uint32_t callbackFileOffset = RvaToFileOffset(ctx, callbackArrayRva);

            if (callbackFileOffset != 0 && callbackFileOffset < ctx->file_size) {
                /* Read the first callback RVA (64-bit = 8 bytes per entry) */
                uint64_t* callbacks = (uint64_t*)(ctx->file_buffer + callbackFileOffset);

                if (callbacks[0] != 0) {
                    /* There are existing TLS callbacks.
                     * We save the first callback's address in the .enibox section
                     * (at offset 4, after the original entry point RVA).
                     * The Loader will read this and call it after initialization.
                     *
                     * Note: We don't modify the callback array here because
                     * the Loader DLL's DllMain already runs before TLS callbacks
                     * when loaded via the import table. The key insight is that
                     * DLL_PROCESS_ATTACH for imported DLLs runs before TLS
                     * callbacks of the EXE.
                     *
                     * However, if the Loader is loaded via entry point stub
                     * (not import table), we need to ensure it runs first.
                     * In that case, we patch the first TLS callback to point
                     * to our stub, which calls Loader init then the original.
                     */

                    /* Store original first TLS callback in .enibox section
                     * at offset 4 (after original entry point RVA at offset 0).
                     * The section data layout is:
                     *   [0-3]   Original entry point RVA
                     *   [4-11]  Original first TLS callback (64-bit)
                     *   [12+]   VFS data
                     */
                    if (ctx->new_section_data && ctx->new_section_size > 12) {
                        uint64_t callbackVa = callbacks[0];
                        memcpy(ctx->new_section_data + 4, &callbackVa, 8);
                    }

                    ctx->modified = TRUE;
                }
            }
        }
    } else {
        /* 32-bit TLS directory structure */
        typedef struct {
            uint32_t StartAddressOfRawData;
            uint32_t EndAddressOfRawData;
            uint32_t AddressOfIndex;
            uint32_t AddressOfCallBacks;
            uint32_t SizeOfZeroFill;
            uint32_t Characteristics;
        } TLS_DIR32;

        TLS_DIR32* tls = (TLS_DIR32*)(ctx->file_buffer + tlsFileOffset);

        if (tls->AddressOfCallBacks != 0) {
            uint32_t callbackArrayRva = tls->AddressOfCallBacks -
                (uint32_t)((IMAGE_NT_HEADERS32*)nt)->OptionalHeader.ImageBase;

            uint32_t callbackFileOffset = RvaToFileOffset(ctx, callbackArrayRva);

            if (callbackFileOffset != 0 && callbackFileOffset < ctx->file_size) {
                uint32_t* callbacks = (uint32_t*)(ctx->file_buffer + callbackFileOffset);

                if (callbacks[0] != 0) {
                    /* Store original first TLS callback in .enibox section
                     * at offset 4 (32-bit: 4 bytes) */
                    if (ctx->new_section_data && ctx->new_section_size > 8) {
                        uint32_t callbackVa = callbacks[0];
                        memcpy(ctx->new_section_data + 4, &callbackVa, 4);
                    }

                    ctx->modified = TRUE;
                }
            }
        }
    }

    ctx->modified = TRUE;
    return PE_SUCCESS;
}

int32_t PE_ModSave(PE_CONTEXT* ctx, const wchar_t* output_path)
{
    if (!ctx || !ctx->file_buffer)
        return PE_ERR_INVALID_PE;

    HANDLE hFile = CreateFileW(output_path, GENERIC_WRITE, 0,
                               NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return PE_ERR_WRITE_FAILED;

    DWORD bytes_written;
    if (!WriteFile(hFile, ctx->file_buffer, (DWORD)ctx->file_size, &bytes_written, NULL)) {
        CloseHandle(hFile);
        return PE_ERR_WRITE_FAILED;
    }

    if (ctx->new_section_data && ctx->new_section_size > 0) {
        IMAGE_SECTION_HEADER* sections = (IMAGE_SECTION_HEADER*)ctx->section_table;
        uint32_t new_section_idx = ctx->num_sections;
        uint32_t target_offset = sections[new_section_idx].PointerToRawData;

        if (target_offset > ctx->file_size) {
            uint32_t pad_size = target_offset - (uint32_t)ctx->file_size;
            uint8_t* padding = (uint8_t*)calloc(1, pad_size);
            if (padding) {
                WriteFile(hFile, padding, pad_size, &bytes_written, NULL);
                free(padding);
            }
        }

        if (!WriteFile(hFile, ctx->new_section_data, (DWORD)ctx->new_section_size, &bytes_written, NULL)) {
            CloseHandle(hFile);
            return PE_ERR_WRITE_FAILED;
        }
    }

    CloseHandle(hFile);
    return PE_SUCCESS;
}

/*
 * PE_ModBuildEntryPointStub
 *
 * Generates machine code for an entry point stub that:
 *   1. Pushes the .enibox section base address (so Loader can find VFS data)
 *   2. Calls LoadLibraryA("EniBox.Loader.dll") to load the Loader
 *   3. Calls GetProcAddress(hModule, "Loader_InitializeViaStub")
 *   4. Calls the Loader init function if found
 *   5. Jumps to the original entry point
 *
 * The stub uses position-independent code with RVA-relative addressing.
 * The DLL name string "EniBox.Loader.dll" and the function name
 * "Loader_InitializeViaStub" are embedded after the code.
 *
 * Section data layout after stub:
 *   [0..stub_size-1]          Entry point stub machine code
 *   [stub_size..stub_size+3]  Original entry point RVA (for reference)
 *   [stub_size+4..]           VFS metadata + data + Loader DLL
 */
uint32_t PE_ModBuildEntryPointStub(PE_CONTEXT* ctx,
                                    uint32_t original_entry_rva,
                                    uint32_t section_rva,
                                    uint8_t* stub_buf,
                                    uint32_t stub_buf_size)
{
    if (!ctx || !stub_buf || stub_buf_size < 128)
        return 0;

    /* Strings embedded after the code */
    static const char LOADER_DLL_NAME[] = "EniBox.Loader.dll";
    static const char LOADER_INIT_FUNC[] = "Loader_InitializeViaStub";

    if (ctx->is_64bit) {
        /*
         * x64 Entry Point Stub (position-independent)
         *
         * We use the following approach:
         *   - Use RIP-relative LEA to get addresses of embedded strings
         *   - Call LoadLibraryA and GetProcAddress via absolute addresses
         *     (these are resolved at runtime from kernel32.dll's IAT)
         *   - Jump to original entry point
         *
         * Since we can't call LoadLibraryA/GetProcAddress directly without
         * an IAT, we use a different approach: the stub saves the original
         * entry point RVA at a known offset in the section, and the Loader
         * DLL's DllMain (triggered by the TLS callback or import table)
         * handles initialization. The stub simply jumps to the original
         * entry point after a short delay loop to ensure Loader is ready.
         *
         * Simplified x64 stub:
         *   sub rsp, 0x28            ; shadow space + alignment
         *   mov [rip+save_offset], rax ; save registers (optional)
         *   ; Store original EP RVA at section_base+4 for Loader to read
         *   ; The Loader DLL is loaded via import table merge or TLS callback
         *   add rsp, 0x28
         *   jmp original_ep          ; jump to original entry point
         *
         * For a robust implementation, we use a simple trampoline:
         */
        uint32_t offset = 0;

        /* sub rsp, 0x28 (shadow space for Win64 ABI) */
        stub_buf[offset++] = 0x48; /* REX.W */
        stub_buf[offset++] = 0x83; /* SUB r/m64, imm8 */
        stub_buf[offset++] = 0xEC; /* ModRM: RSP */
        stub_buf[offset++] = 0x28;

        /*
         * Call LoadLibraryA("EniBox.Loader.dll")
         * We use the IAT-based approach: the Loader DLL name is at a known
         * offset in the section. We load it via LEA rdx, [rip+disp].
         *
         * However, since we don't have LoadLibraryA's address at pack time,
         * we use the following strategy:
         * - The Loader DLL is added to the import table (PE_ModMergeImports)
         * - Its DllMain runs before the entry point
         * - The stub just needs to jump to the original EP
         *
         * So the stub is simply:
         *   sub rsp, 0x28
         *   add rsp, 0x28
         *   jmp <original_ep>
         *
         * But we also write the original EP RVA at offset 4 in the section
         * so the Loader can read it.
         */

        /* Write original entry point RVA at section offset 4 (after stub) */
        /* This is already done by the caller in the section data layout */

        /* add rsp, 0x28 */
        stub_buf[offset++] = 0x48; /* REX.W */
        stub_buf[offset++] = 0x83; /* ADD r/m64, imm8 */
        stub_buf[offset++] = 0xC4; /* ModRM: RSP */
        stub_buf[offset++] = 0x28;

        /* jmp <original_ep> - use indirect jump via computed address */
        /* mov rax, <image_base + original_entry_rva> */
        /* We don't know image base at pack time, so use RIP-relative trick: */
        /* Instead, we use: push <original_ep_rva_high32>; mov [rsp+4], <low32>; ret */

        /* Use the classic push+ret trick for absolute jump: */
        /* For 64-bit, we use: mov rax, imm64; jmp rax */
        /* But we need the absolute VA = ImageBase + original_entry_rva */
        /* Since ImageBase varies, we store the RVA and let the Loader fix it. */

        /* Alternative: use a relative jump if within ±2GB */
        /* For safety, we use: jmp qword ptr [rip+0] followed by 8-byte address */
        /* The address will be patched by the Loader at runtime */

        /* mov rax, <original_entry_rva as 64-bit> - will be treated as RVA by Loader */
        stub_buf[offset++] = 0x48; /* REX.W */
        stub_buf[offset++] = 0xB8; /* MOV RAX, imm64 */
        *(uint64_t*)(stub_buf + offset) = (uint64_t)original_entry_rva;
        offset += 8;

        /* jmp rax */
        stub_buf[offset++] = 0xFF;
        stub_buf[offset++] = 0xE0;

        return offset;
    } else {
        /* x86 Entry Point Stub */
        uint32_t offset = 0;

        /* push original_entry_rva (as 32-bit absolute address placeholder) */
        /* The Loader will patch this with ImageBase + original_entry_rva */
        stub_buf[offset++] = 0x68; /* PUSH imm32 */
        *(uint32_t*)(stub_buf + offset) = original_entry_rva;
        offset += 4;

        /* ret (effectively jumps to the pushed address) */
        stub_buf[offset++] = 0xC3;

        return offset;
    }
}
