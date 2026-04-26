#ifndef LOADER_MAIN_H
#define LOADER_MAIN_H
#include <stdint.h>
int32_t Loader_Initialize(uint8_t* vfs_base, uint32_t vfs_size);
void     Loader_Finalize(void);
#endif
