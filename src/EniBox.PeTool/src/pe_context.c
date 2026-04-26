#include "pe_context.h"
#include <stdlib.h>
#include <string.h>

PE_CONTEXT* PE_ContextCreate(void)
{
    PE_CONTEXT* ctx = (PE_CONTEXT*)calloc(1, sizeof(PE_CONTEXT));
    if (ctx) {
        ctx->file_buffer = NULL;
        ctx->file_size = 0;
        ctx->modified = FALSE;
        ctx->new_section_data = NULL;
        ctx->new_section_size = 0;
        memset(ctx->new_section_name, 0, sizeof(ctx->new_section_name));
    }
    return ctx;
}

void PE_ContextDestroy(PE_CONTEXT* ctx)
{
    if (ctx) {
        if (ctx->file_buffer) {
            free(ctx->file_buffer);
            ctx->file_buffer = NULL;
        }
        if (ctx->new_section_data) {
            free(ctx->new_section_data);
            ctx->new_section_data = NULL;
        }
        free(ctx);
    }
}

int32_t PE_ContextLoadFile(PE_CONTEXT* ctx, const wchar_t* file_path)
{
    HANDLE hFile = CreateFileW(file_path, GENERIC_READ, FILE_SHARE_READ,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return PE_ERR_FILE_NOT_FOUND;

    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(hFile, &file_size)) {
        CloseHandle(hFile);
        return PE_ERR_READ_FAILED;
    }

    ctx->file_size = (size_t)file_size.QuadPart;
    ctx->file_buffer = (uint8_t*)malloc(ctx->file_size);
    if (!ctx->file_buffer) {
        CloseHandle(hFile);
        return PE_ERR_NO_MEMORY;
    }

    DWORD bytes_read;
    if (!ReadFile(hFile, ctx->file_buffer, (DWORD)ctx->file_size, &bytes_read, NULL) ||
        bytes_read != ctx->file_size) {
        free(ctx->file_buffer);
        ctx->file_buffer = NULL;
        CloseHandle(hFile);
        return PE_ERR_READ_FAILED;
    }

    CloseHandle(hFile);
    return PE_SUCCESS;
}
