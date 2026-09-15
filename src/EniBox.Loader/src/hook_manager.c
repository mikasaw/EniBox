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
    /* NT-level hooks verified viable on Win11 26200 with upstream MinHook
     * (probe 2026-09-15: VfsTest 9/9 with pass-through traffic crossing all
     * three detours). The old D3 disable was a stale-loader misdiagnosis. */
    r = HookNt_Install(); if (r != 0) return r;
    r = HookFind_Install(); if (r != 0) return r;
    r = HookMapping_Install(); if (r != 0) return r;
    return 0;
}
int32_t Hook_InstallProcessHooks(void) {
    /* Verified viable on Win11 26200 (probe 2026-09-15: W/A-family children
     * created through the detour; the injected Loader confirmed inside the
     * child via debugger). The old D3 disable was a stale-loader misdiag. */
    return HookProcess_Install();
}
int32_t Hook_InstallRegistryHooks(void) { return HookRegistry_Install(); }
