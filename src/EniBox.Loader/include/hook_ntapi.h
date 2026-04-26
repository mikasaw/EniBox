#ifndef HOOK_NTAPI_H
#define HOOK_NTAPI_H
#include <winternl.h>
#include <stdint.h>
int32_t HookNt_Install(void);
#endif
