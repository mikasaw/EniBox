#include "hook_manager.h"
#include "hook_fileapi.h"
#include "hook_ntapi.h"
#include "hook_findfile.h"
#include "hook_filemapping.h"
#include "hook_process.h"
#include "hook_registry.h"
#include "../deps/MinHook/include/MinHook.h"
#include <stdlib.h>

static BOOL g_hooks_initialized = FALSE;
static BOOL g_hooks_enabled = FALSE;

int32_t Hook_Initialize(void) {
    if (MH_Initialize() != MH_OK) return -1;
    g_hooks_initialized = TRUE;
    return 0;
}
int32_t Hook_EnableAll(void) {
    if (!g_hooks_initialized) return -1;
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) return -2;
    g_hooks_enabled = TRUE;
    return 0;
}
int32_t Hook_DisableAll(void) {
    if (!g_hooks_initialized) return -1;
    if (MH_DisableHook(MH_ALL_HOOKS) != MH_OK) return -2;
    g_hooks_enabled = FALSE;
    return 0;
}
void Hook_Uninitialize(void) {
    if (!g_hooks_initialized) return;
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    g_hooks_initialized = FALSE;
    g_hooks_enabled = FALSE;
}
int32_t Hook_InstallFileHooks(void) {
    int32_t r = HookFile_Install(); if (r != 0) return r;
    /* NT-level (NtCreateFile/NtOpenFile/NtReadFile) inline hooks are disabled:
     * Win11 25H2+ syscall stubs contain instance-dependent integrity
     * instructions that cannot be copied into a trampoline (D3). Win32-level
     * hooks cover normal Win32 applications. */
    r = HookFind_Install(); if (r != 0) return r;
    r = HookMapping_Install(); if (r != 0) return r;
    return 0;
}
int32_t Hook_InstallProcessHooks(void) {
    /* Disabled: the process-creation chain has the same trampoline gap (D3). */
    return 0;
}
int32_t Hook_InstallRegistryHooks(void) { return HookRegistry_Install(); }
