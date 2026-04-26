#include "pe_parser.h"
#include "pe_context.h"
#include <string.h>

int32_t PE_ParseDosHeader(PE_CONTEXT* ctx)
{
    if (!ctx || !ctx->file_buffer || ctx->file_size < 64)
        return PE_ERR_INVALID_PE;

    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)ctx->file_buffer;
    if (dos->e_magic != DOS_SIGNATURE)
        return PE_ERR_INVALID_PE;

    ctx->dos_header = dos;
    ctx->pe_offset = (size_t)dos->e_lfanew;

    if (ctx->pe_offset >= ctx->file_size || ctx->pe_offset < 64)
        return PE_ERR_INVALID_PE;

    return PE_SUCCESS;
}

int32_t PE_ParseNtHeaders(PE_CONTEXT* ctx)
{
    if (!ctx || !ctx->file_buffer)
        return PE_ERR_INVALID_PE;

    uint32_t* sig = (uint32_t*)(ctx->file_buffer + ctx->pe_offset);
    if (*sig != PE_SIGNATURE)
        return PE_ERR_INVALID_PE;

    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(ctx->file_buffer + ctx->pe_offset);
    ctx->nt_headers = nt;
    ctx->machine = nt->FileHeader.Machine;
    ctx->is_64bit = (nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC);

    if (ctx->is_64bit) {
        IMAGE_NT_HEADERS64* nt64 = (IMAGE_NT_HEADERS64*)nt;
        ctx->entry_point_rva = nt64->OptionalHeader.AddressOfEntryPoint;
        ctx->size_of_image = nt64->OptionalHeader.SizeOfImage;
        ctx->size_of_headers = nt64->OptionalHeader.SizeOfHeaders;
        ctx->file_alignment = nt64->OptionalHeader.FileAlignment;
        ctx->section_alignment = nt64->OptionalHeader.SectionAlignment;
        ctx->optional_header = &nt64->OptionalHeader;
    } else {
        IMAGE_NT_HEADERS32* nt32 = (IMAGE_NT_HEADERS32*)nt;
        ctx->entry_point_rva = nt32->OptionalHeader.AddressOfEntryPoint;
        ctx->size_of_image = nt32->OptionalHeader.SizeOfImage;
        ctx->size_of_headers = nt32->OptionalHeader.SizeOfHeaders;
        ctx->file_alignment = nt32->OptionalHeader.FileAlignment;
        ctx->section_alignment = nt32->OptionalHeader.SectionAlignment;
        ctx->optional_header = &nt32->OptionalHeader;
    }

    ctx->num_sections = nt->FileHeader.NumberOfSections;
    return PE_SUCCESS;
}

int32_t PE_ParseSectionTable(PE_CONTEXT* ctx)
{
    if (!ctx || !ctx->nt_headers)
        return PE_ERR_INVALID_PE;

    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)ctx->nt_headers;
    ctx->section_table = (IMAGE_SECTION_HEADER*)((uint8_t*)nt +
        sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) + nt->FileHeader.SizeOfOptionalHeader);
    return PE_SUCCESS;
}

int32_t PE_Validate(PE_CONTEXT* ctx)
{
    int32_t result;
    result = PE_ParseDosHeader(ctx);
    if (result != PE_SUCCESS) return result;
    result = PE_ParseNtHeaders(ctx);
    if (result != PE_SUCCESS) return result;
    result = PE_ParseSectionTable(ctx);
    if (result != PE_SUCCESS) return result;
    if (ctx->machine != IMAGE_FILE_MACHINE_I386 && ctx->machine != IMAGE_FILE_MACHINE_AMD64)
        return PE_ERR_UNSUPPORTED_ARCH;
    return PE_SUCCESS;
}
