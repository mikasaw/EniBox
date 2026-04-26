#ifndef LOADER_CRC32_H
#define LOADER_CRC32_H
#include <stdint.h>
uint32_t CRC32_Compute(const uint8_t* data, size_t length);
#endif
