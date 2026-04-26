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

/* Build an entry point stub that jumps to the original entry point.
 * The jump target is a VA placeholder that the Loader patches at runtime.
 * Section layout: [stub code][original_ep_rva:4][section_rva:4][VFS data...]
 * x64: jmp [rip+0] + 8-byte placeholder (offset 14 = VA placeholder)
 * x86: push imm32 + ret (offset 1 = VA placeholder)
 * Returns total stub+metadata size in bytes (0 on error).
 * stub_buf must be at least 128 bytes. */
uint32_t PE_ModBuildEntryPointStub(PE_CONTEXT* ctx,
                                    uint32_t original_entry_rva,
                                    uint32_t section_rva,
                                    uint8_t* stub_buf,
                                    uint32_t stub_buf_size);

#endif
