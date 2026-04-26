#ifndef HOOK_MANAGER_H
#define HOOK_MANAGER_H
#include <windows.h>
#include <stdint.h>
int32_t Hook_Initialize(void);
int32_t Hook_EnableAll(void);
int32_t Hook_DisableAll(void);
void Hook_Uninitialize(void);
int32_t Hook_InstallFileHooks(void);
int32_t Hook_InstallProcessHooks(void);
int32_t Hook_InstallRegistryHooks(void);
#endif
