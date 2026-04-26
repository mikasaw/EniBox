#ifndef PE_MODIFIER_H
#define PE_MODIFIER_H

#include "pe_types.h"

int32_t PE_ModAddSection(PE_CONTEXT* ctx, const char* name,
                          const uint8_t* data, uint32_t data_size,
                          uint32_t characteristics);
int32_t PE_ModSetEntryPoint(PE_CONTEXT* ctx, uint32_t new_entry_rva);
int32_t PE_ModMergeImports(PE_CONTEXT* ctx, const IMPORT_ENTRY* entries, uint32_t count);
int32_t PE_ModProcessTLS(PE_CONTEXT* ctx);
int32_t PE_ModSave(PE_CONTEXT* ctx, const wchar_t* output_path);

/* Build an entry point stub that loads EniBox.Loader.dll via LoadLibraryA,
 * calls GetProcAddress for "Loader_InitializeViaStub", then jumps to
 * the original entry point. Returns stub size in bytes (0 on error).
 * stub_buf must be at least 64 bytes. */
uint32_t PE_ModBuildEntryPointStub(PE_CONTEXT* ctx,
                                    uint32_t original_entry_rva,
                                    uint32_t section_rva,
                                    uint8_t* stub_buf,
                                    uint32_t stub_buf_size);

#endif
