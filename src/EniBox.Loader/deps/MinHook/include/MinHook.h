#ifndef MINHOOK_H
#define MINHOOK_H
#include <windows.h>

typedef enum MH_STATUS {
    MH_OK = 0,
    MH_ERROR_ALREADY_INITIALIZED,
    MH_ERROR_NOT_INITIALIZED,
    MH_ERROR_UNSUPPORTED_FUNCTION,
    MH_ERROR_MEMORY_ALLOC,
    MH_ERROR_MEMORY_PROTECT,
    MH_ERROR_HOOK_NOT_FOUND,
    MH_ERROR_ENABLED_HOOK_EXISTS,
    MH_ERROR_UNSUPPORTED_OS,
    MH_ERROR_MAX_HOOKS
} MH_STATUS;

#ifdef __cplusplus
extern "C" {
#endif

MH_STATUS MH_Initialize(void);
MH_STATUS MH_Uninitialize(void);
MH_STATUS MH_CreateHook(void* pTarget, void* pDetour, void** ppOriginal);
MH_STATUS MH_EnableHook(void* pTarget);
MH_STATUS MH_DisableHook(void* pTarget);
MH_STATUS MH_QueueHook(void* pTarget);
MH_STATUS MH_DequeueHook(void* pTarget);
MH_STATUS MH_ApplyQueued(void);

#define MH_ALL_HOOKS NULL

#ifdef __cplusplus
}
#endif

#endif
