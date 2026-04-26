#ifndef HOOK_PROCESS_H
#define HOOK_PROCESS_H
#include <windows.h>
#include <stdint.h>

/* Process hook installation - hooks CreateProcessA/W to inject Loader DLL
 * into child processes, ensuring VFS virtualization propagates. */
int32_t HookProcess_Install(void);

/* Get the current Loader DLL path for injection */
const wchar_t* HookProcess_GetLoaderPath(void);

/* Set the Loader DLL path (called during DllMain initialization) */
void HookProcess_SetLoaderPath(const wchar_t* path);

#endif
