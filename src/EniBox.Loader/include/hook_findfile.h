#ifndef HOOK_FINDFILE_H
#define HOOK_FINDFILE_H
#include <windows.h>
#include <stdint.h>

/* Virtual find handle - shared with hook_filemapping.c for CloseHandle dispatch */
#define MAX_FIND_HANDLES 256
#define FIND_HANDLE_BASE 0xEFFF0000

typedef struct _VFIND_HANDLE {
    BOOL          inUse;
    uint32_t*     matchIndices;   /* Array of matching VFS file indices */
    uint32_t      matchCount;     /* Total matches */
    uint32_t      matchPos;       /* Current position */
    char          searchDir[260]; /* Directory being searched */
    char          pattern[260];   /* Search pattern (e.g., "*.dll") */
} VFIND_HANDLE;

/* Find handle management - used by CloseHandle hook in hook_filemapping.c */
VFIND_HANDLE* HookFind_GetHandle(HANDLE h);
void HookFind_FreeHandle(HANDLE h);

/* Hook installation */
int32_t HookFind_Install(void);

#endif
