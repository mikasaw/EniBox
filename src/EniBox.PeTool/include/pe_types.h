#ifndef PE_TYPES_H
#define PE_TYPES_H

#include <stdint.h>
#include <windows.h>

#define PE_SIGNATURE 0x00004550
#define DOS_SIGNATURE 0x5A4D
#ifndef IMAGE_FILE_MACHINE_I386
#define IMAGE_FILE_MACHINE_I386  0x014C
#endif
#ifndef IMAGE_FILE_MACHINE_AMD64
#define IMAGE_FILE_MACHINE_AMD64 0x8664
#endif
#define ENIBOX_SECTION_NAME ".enibox"

#define PE_SUCCESS              0
#define PE_ERR_FILE_NOT_FOUND   1001
#define PE_ERR_INVALID_PE       2001
#define PE_ERR_UNSUPPORTED_ARCH 2002
#define PE_ERR_SECTION_FULL     5001
#define PE_ERR_IMPORT_MERGE     5002
#define PE_ERR_WRITE_FAILED     5003
#define PE_ERR_READ_FAILED      5004
#define PE_ERR_NO_MEMORY        5005

typedef struct _PE_CONTEXT {
    uint8_t*     file_buffer;
    size_t       file_size;
    size_t       pe_offset;
    BOOL         is_64bit;
    uint16_t     machine;
    uint32_t     entry_point_rva;
    uint32_t     num_sections;
    uint32_t     size_of_image;
    uint32_t     size_of_headers;
    uint32_t     file_alignment;
    uint32_t     section_alignment;
    void*        dos_header;
    void*        nt_headers;
    void*        section_table;
    void*        optional_header;
    BOOL         modified;
    uint8_t*     new_section_data;
    size_t       new_section_size;
    char         new_section_name[8];
} PE_CONTEXT;

typedef struct _PE_INFO {
    uint32_t machine;
    uint32_t entry_point_rva;
    uint32_t num_sections;
    uint32_t size_of_image;
    uint32_t size_of_headers;
} PE_INFO;

typedef struct _IMPORT_ENTRY {
    char     dll_name[256];
} IMPORT_ENTRY;

#endif
