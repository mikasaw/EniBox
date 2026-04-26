#ifndef LZMA_DEC_H
#define LZMA_DEC_H
#include <stdint.h>
int32_t LzmaDec_Decompress(const uint8_t* compressed, uint32_t compressed_size,
                             uint8_t* output, uint32_t* output_size);
#endif
