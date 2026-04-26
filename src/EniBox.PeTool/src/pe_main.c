#include "pe_exports.h"
#include "pe_parser.h"
#include "pe_modifier.h"
#include "pe_context.h"
#include <stdlib.h>
#include <string.h>

int32_t PE_Open(const wchar_t* pe_path, PE_CONTEXT** ctx)
{
    if (!pe_path || !ctx)
        return PE_ERR_FILE_NOT_FOUND;

    PE_CONTEXT* new_ctx = PE_ContextCreate();
    if (!new_ctx)
        return PE_ERR_NO_MEMORY;

    int32_t result = PE_ContextLoadFile(new_ctx, pe_path);
    if (result != PE_SUCCESS) {
        PE_ContextDestroy(new_ctx);
        return result;
    }

    result = PE_Validate(new_ctx);
    if (result != PE_SUCCESS) {
        PE_ContextDestroy(new_ctx);
        return result;
    }

    *ctx = new_ctx;
    return PE_SUCCESS;
}

int32_t PE_GetInfo(PE_CONTEXT* ctx, PE_INFO* info)
{
    if (!ctx || !info)
        return PE_ERR_INVALID_PE;

    info->machine = ctx->machine;
    info->entry_point_rva = ctx->entry_point_rva;
    info->num_sections = ctx->num_sections;
    info->size_of_image = ctx->size_of_image;
    info->size_of_headers = ctx->size_of_headers;
    return PE_SUCCESS;
}

int32_t PE_AddSection(PE_CONTEXT* ctx, const char* name,
                       const uint8_t* data, uint32_t data_size,
                       uint32_t characteristics)
{
    return PE_ModAddSection(ctx, name, data, data_size, characteristics);
}

int32_t PE_SetEntryPoint(PE_CONTEXT* ctx, uint32_t new_entry_rva)
{
    return PE_ModSetEntryPoint(ctx, new_entry_rva);
}

int32_t PE_MergeImports(PE_CONTEXT* ctx, const IMPORT_ENTRY* entries, uint32_t count)
{
    return PE_ModMergeImports(ctx, entries, count);
}

int32_t PE_ProcessTLS(PE_CONTEXT* ctx)
{
    return PE_ModProcessTLS(ctx);
}

int32_t PE_Save(PE_CONTEXT* ctx, const wchar_t* output_path)
{
    return PE_ModSave(ctx, output_path);
}

void PE_Close(PE_CONTEXT* ctx)
{
    PE_ContextDestroy(ctx);
}
