#ifndef PE_PARSER_H
#define PE_PARSER_H

#include "pe_types.h"

int32_t PE_ParseDosHeader(PE_CONTEXT* ctx);
int32_t PE_ParseNtHeaders(PE_CONTEXT* ctx);
int32_t PE_ParseSectionTable(PE_CONTEXT* ctx);
int32_t PE_Validate(PE_CONTEXT* ctx);

#endif
