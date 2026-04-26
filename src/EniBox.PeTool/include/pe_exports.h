#ifndef PE_EXPORTS_H
#define PE_EXPORTS_H

#include "pe_types.h"

#ifdef ENIBOX_PETOOL_EXPORTS
#define PE_API __declspec(dllexport)
#else
#define PE_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

PE_API int32_t PE_Open(const wchar_t* pe_path, PE_CONTEXT** ctx);
PE_API int32_t PE_GetInfo(PE_CONTEXT* ctx, PE_INFO* info);
PE_API int32_t PE_AddSection(PE_CONTEXT* ctx, const char* name,
                              const uint8_t* data, uint32_t data_size,
                              uint32_t characteristics);
PE_API int32_t PE_SetEntryPoint(PE_CONTEXT* ctx, uint32_t new_entry_rva);
PE_API int32_t PE_MergeImports(PE_CONTEXT* ctx, const IMPORT_ENTRY* entries, uint32_t count);
PE_API int32_t PE_ProcessTLS(PE_CONTEXT* ctx);
PE_API int32_t PE_Save(PE_CONTEXT* ctx, const wchar_t* output_path);
PE_API void PE_Close(PE_CONTEXT* ctx);

#ifdef __cplusplus
}
#endif

#endif
