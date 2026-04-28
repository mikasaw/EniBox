/*
 * loader_errors.h - Unified error codes for EniBox Loader
 *
 * Error code ranges:
 *   0          = Success
 *   6001-6099  = VFS runtime errors
 *   6101-6199  = VFS handle table errors
 *   6201-6299  = VFS hash table errors
 *   6301-6399  = LZMA decompression errors
 *   6401-6499  = Hook installation errors
 *   6501-6599  = Hook manager errors
 *   6601-6699  = Injection errors
 *   6701-6799  = Registry hook errors
 *
 * This aligns with PeTool's PE_ERR_* range (1001-5005) and avoids overlap.
 */

#ifndef LOADER_ERRORS_H
#define LOADER_ERRORS_H

/* Success */
#define LOADER_OK               0

/* VFS runtime errors (6001-6099) */
#define VFS_ERR_INVALID_PARAM   6001
#define VFS_ERR_INVALID_MAGIC   6002
#define VFS_ERR_VERSION         6003
#define VFS_ERR_CHECKSUM        6004
#define VFS_ERR_HANDLE_INIT     6005
#define VFS_ERR_HASHTABLE_INIT  6006
#define VFS_ERR_NULL_CONTEXT    6007
#define VFS_ERR_DECOMPRESS      6008

/* VFS handle table errors (6101-6199) */
#define VFS_ERR_HANDLE_TABLE_FULL  6101

/* VFS hash table errors (6201-6299) */
#define VFS_ERR_HT_INVALID_CTX  6201
#define VFS_ERR_HT_NO_TABLE     6202
#define VFS_ERR_HT_NO_ENTRY     6203

/* LZMA decompression errors (6301-6399) */
#define LZMA_ERR_INVALID_PARAM  6301
#define LZMA_ERR_DATA_TOO_SHORT 6302
#define LZMA_ERR_SIZE_MISMATCH  6303
#define LZMA_ERR_INVALID_PROPS  6304
#define LZMA_ERR_NO_MEMORY      6305

/* Hook installation errors (6401-6499) */
#define HOOK_ERR_CREATE_FILE_W      6401
#define HOOK_ERR_CREATE_FILE_A      6402
#define HOOK_ERR_READ_FILE          6403
#define HOOK_ERR_GET_FILE_SIZE      6404
#define HOOK_ERR_GET_FILE_SIZE_EX   6405
#define HOOK_ERR_GET_FILE_ATTR_A    6406
#define HOOK_ERR_GET_FILE_ATTR_W    6407
#define HOOK_ERR_WRITE_FILE         6408
#define HOOK_ERR_SET_FILE_PTR       6410
#define HOOK_ERR_SET_FILE_PTR_EX    6411
#define HOOK_ERR_CREATE_FILEMAP_W   6421
#define HOOK_ERR_CREATE_FILEMAP_A   6422
#define HOOK_ERR_MAP_VIEW           6423
#define HOOK_ERR_UNMAP_VIEW         6424
#define HOOK_ERR_CLOSE_HANDLE       6425
#define HOOK_ERR_FIND_FIRST_FILE_W  6431
#define HOOK_ERR_FIND_FIRST_FILE_A  6432
#define HOOK_ERR_FIND_NEXT_FILE_W   6433
#define HOOK_ERR_FIND_NEXT_FILE_A   6434
#define HOOK_ERR_NT_QUERY_DIR       6441
#define HOOK_ERR_NT_QUERY_ATTR      6442
#define HOOK_ERR_NT_OPEN_FILE       6443
#define HOOK_ERR_CREATE_PROC_W      6451
#define HOOK_ERR_CREATE_PROC_A      6452

/* Hook manager errors (6501-6599) */
#define HOOKMGR_ERR_INIT         6501
#define HOOKMGR_ERR_NOT_INIT     6502
#define HOOKMGR_ERR_ENABLE       6503
#define HOOKMGR_ERR_DISABLE      6504

/* Injection errors (6601-6699) */
#define INJECT_ERR_INVALID_PARAM 6601
#define INJECT_ERR_NO_MEMORY     6602
#define INJECT_ERR_WRITE_FAIL    6603
#define INJECT_ERR_NO_MODULE     6604
#define INJECT_ERR_NO_FUNC       6605
#define INJECT_ERR_THREAD_FAIL   6606

/* Registry hook errors (6701-6799) */
#define REG_ERR_INVALID_PARAM    6701
#define REG_ERR_NOT_FOUND        6702
#define REG_ERR_OPEN_KEY_A       6711
#define REG_ERR_OPEN_KEY_W       6712
#define REG_ERR_QUERY_VALUE_A    6713
#define REG_ERR_QUERY_VALUE_W    6714
#define REG_ERR_CLOSE_KEY        6715
#define REG_ERR_ENUM_VALUE_A     6716
#define REG_ERR_ENUM_VALUE_W     6717
#define REG_ERR_SET_VALUE_A      6718
#define REG_ERR_SET_VALUE_W      6719

#endif /* LOADER_ERRORS_H */
