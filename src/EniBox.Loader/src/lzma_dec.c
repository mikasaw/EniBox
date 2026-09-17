/*
 * LZMA Decoder wrapper for EniBox Loader
 *
 * Decompresses the VFS stream produced by the C# LzmaCompressor:
 *   [5 bytes: LZMA properties] [8 bytes: original size LE] [compressed data...]
 * (standard LZMA_ALONE header).
 *
 * The actual decoding uses the official LZMA SDK reference decoder
 * (deps/lzma_sdk/LzmaDec.c, public domain by Igor Pavlov). The previous
 * hand-rolled decoder mis-decoded streams containing match lengths >= 8
 * (broken kLenHigh path returning a constant and reading the probs array
 * out of bounds), sending the input pointer past the section end —
 * 0xC0000005 on Win10 (RUNBOOK 5.1.7).
 */

#include "lzma_dec.h"
#include "loader_errors.h"
#include "../deps/lzma_sdk/LzmaDec.h"
#include <stdlib.h>
#include <stdint.h>

static void *SzAllocImpl(ISzAllocPtr p, size_t size) { (void)p; return malloc(size); }
static void SzFreeImpl(ISzAllocPtr p, void *address) { (void)p; free(address); }
static const ISzAlloc g_SzAlloc = { SzAllocImpl, SzFreeImpl };

int32_t LzmaDec_Decompress(const uint8_t* compressed, uint32_t compressed_size,
                             uint8_t* output, uint32_t* output_size) {
    if (!compressed || !output || !output_size) return LZMA_ERR_INVALID_PARAM;
    if (compressed_size < 18) return LZMA_ERR_DATA_TOO_SHORT; /* props(5)+size(8)+至少 5 字节数据 */

    /* VFS 流 = LZMA_ALONE 头：compressed[0..4] = props + dictSize，
     * compressed[5..12] = original size LE（与 output_size 交叉校验）。 */
    uint64_t declared = 0;
    for (int i = 0; i < 8; i++)
        declared |= (uint64_t)compressed[5 + i] << (8 * i);
    if (declared > *output_size) return LZMA_ERR_SIZE_MISMATCH;

    SizeT destLen = *output_size;
    SizeT srcLen = compressed_size - 13;
    ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;

    SRes res = LzmaDecode(output, &destLen,
                          compressed + 13, &srcLen,
                          compressed, 5,
                          LZMA_FINISH_END, &status, &g_SzAlloc);
    if (res != SZ_OK) return LZMA_ERR_DECOMPRESS;
    if (status != LZMA_STATUS_FINISHED_WITH_MARK &&
        status != LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK) return LZMA_ERR_DECOMPRESS;

    *output_size = (uint32_t)destLen;
    return 0;
}
