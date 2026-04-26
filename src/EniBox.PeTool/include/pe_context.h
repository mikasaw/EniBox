#ifndef PE_CONTEXT_H
#define PE_CONTEXT_H

#include "pe_types.h"

PE_CONTEXT* PE_ContextCreate(void);
void PE_ContextDestroy(PE_CONTEXT* ctx);
int32_t PE_ContextLoadFile(PE_CONTEXT* ctx, const wchar_t* file_path);

#endif
