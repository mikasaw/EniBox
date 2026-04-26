/*
 * LZMA Decoder for EniBox Loader
 * 
 * This implements a standalone LZMA decoder that matches the format
 * produced by the C# LzmaCompressor:
 *   [5 bytes: LZMA properties] [8 bytes: original size LE] [compressed data...]
 *
 * Based on LZMA SDK (7z) public domain code by Igor Pavlov.
 */

#include "lzma_dec.h"
#include <stdlib.h>
#include <string.h>

/* ---- Minimal LZMA decoder (based on LzmaDec from LZMA SDK) ---- */

#define kNumTopBits 24
#define kTopValue ((uint32_t)1 << kNumTopBits)

#define kNumBitModelTotalBits 11
#define kBitModelTotal (1 << kNumBitModelTotalBits)
#define kNumMoveBits 5

#define RC_INIT_SIZE 5

#define kNumPosBitsMax 4
#define kNumPosStatesMax (1 << kNumPosBitsMax)

#define kLenNumLowBits 3
#define kLenNumLowSymbols (1 << kLenNumLowBits)
#define kLenNumMidBits 3
#define kLenNumMidSymbols (1 << kLenNumMidBits)
#define kLenNumHighBits 8
#define kLenNumHighSymbols (1 << kLenNumHighBits)

#define kLenChoice 0
#define kLenChoice2 (kLenChoice + 1)
#define kLenLow (kLenChoice2 + 1)
#define kLenMid (kLenLow + (kNumPosStatesMax << kLenNumLowBits))
#define kLenHigh (kLenMid + (kNumPosStatesMax << kLenNumMidBits))
#define kNumLenProbs (kLenHigh + kLenNumHighSymbols)

#define kNumStates 12
#define kNumLitStates 2
#define kStartPosModelIndex 4
#define kEndPosModelIndex 14
#define kNumFullDistances (1 << (kEndPosModelIndex >> 1))

#define kNumPosSlotBits 6
#define kNumLenToPosStates 4

#define kNumAlignBits 4
#define kAlignTableSize (1 << kNumAlignBits)

#define kMatchMinLen 2
#define kMatchSpecLenStart (kMatchMinLen + kLenNumLowSymbols + kLenNumMidSymbols + kLenNumHighSymbols)

#define IsMatch(st, posState) ((st) * (kNumLenToPosStates * 2 + kAlignTableSize) + (posState) * 2)
#define IsRep(st) (IsMatch(st, 0) + kNumLenToPosStates * 2)
#define IsRepG0(st) (IsRep(st) + 1)
#define IsRepG1(st) (IsRepG0(st) + 1)
#define IsRepG2(st) (IsRepG1(st) + 1)
#define IsRep0Long(st, posState) (IsRepG2(st) + 1 + (posState) * 2)
#define RepLenCoder(st) (IsRep0Long(st, 0) + kNumLenToPosStates * 2)
#define PosSlot(st, lenState) (RepLenCoder(st) + kNumLenProbs + (lenState) * kNumPosSlotBits)
#define SpecPos(st, posSlot) (PosSlot(st, 0) + kNumLenToPosStates * kNumPosSlotBits + ((posSlot) >= kStartPosModelIndex ? ((posSlot) >> 1) - 1 : 0))
#define AlignProbs(st) (SpecPos(st, kEndPosModelIndex - 1) + kNumFullDistances - kEndPosModelIndex)
#define LitProbs(st, prevByte) (AlignProbs(st) + kAlignTableSize + ((st) < kNumLitStates ? 0 : ((prevByte) >> (8 - kNumLitStates)) * 0x300))

#define kNumProbs (LitProbs(kNumStates - 1, 0xFF) + 0x300)

typedef uint16_t CLzmaProb;

typedef struct {
    const uint8_t* buf;
    uint32_t range;
    uint32_t code;
    uint32_t bufPos;
    uint32_t bufLimit;
} CRangeDec;

static void RangeDec_Init(CRangeDec* p) {
    p->code = 0;
    p->range = 0xFFFFFFFF;
    for (int i = 0; i < 5; i++)
        p->code = (p->code << 8) | p->buf[p->bufPos++];
}

static int RangeDec_DecodeBit(CRangeDec* p, CLzmaProb* prob) {
    uint32_t bound = (p->range >> kNumBitModelTotalBits) * *prob;
    if (p->code < bound) {
        p->range = bound;
        *prob += (kBitModelTotal - *prob) >> kNumMoveBits;
        if (p->range < kTopValue) {
            p->range <<= 8;
            p->code = (p->code << 8) | p->buf[p->bufPos++];
        }
        return 0;
    } else {
        p->range -= bound;
        p->code -= bound;
        *prob -= *prob >> kNumMoveBits;
        if (p->range < kTopValue) {
            p->range <<= 8;
            p->code = (p->code << 8) | p->buf[p->bufPos++];
        }
        return 1;
    }
}

static uint32_t RangeDec_DecodeDirectBits(CRangeDec* p, int numBits) {
    uint32_t res = 0;
    do {
        p->range >>= 1;
        p->code -= p->range;
        uint32_t t = 0 - ((uint32_t)p->code >> 31);
        p->code += p->range & t;
        res <<= 1;
        res += t + 1;
        if (p->range < kTopValue) {
            p->range <<= 8;
            p->code = (p->code << 8) | p->buf[p->bufPos++];
        }
    } while (--numBits);
    return res;
}

static void LzmaDec_InitProbs(CLzmaProb* probs) {
    for (uint32_t i = 0; i < kNumProbs; i++)
        probs[i] = kBitModelTotal >> 1;
}

static uint32_t DecodeLen(CRangeDec* rc, CLzmaProb* probs, uint32_t posState) {
    if (RangeDec_DecodeBit(rc, &probs[kLenChoice]) == 0)
        return RangeDec_DecodeBit(rc, &probs[kLenLow + posState * kLenNumLowSymbols]);
    if (RangeDec_DecodeBit(rc, &probs[kLenChoice2]) == 0)
        return kLenNumLowSymbols + RangeDec_DecodeBit(rc, &probs[kLenMid + posState * kLenNumMidSymbols]);
    return kLenNumLowSymbols + kLenNumMidSymbols +
           RangeDec_DecodeBit(rc, &probs[kLenHigh + kLenNumHighSymbols]);
}

static int LzmaDec_DecodeReal(CLzmaProb* probs, uint32_t state,
                               uint32_t rep0, uint32_t rep1, uint32_t rep2, uint32_t rep3,
                               uint8_t* dic, uint32_t* dicPos, uint32_t dicBufSize,
                               uint8_t prevByte, CRangeDec* rc) {
    for (;;) {
        uint32_t posState = *dicPos & ((1 << 4) - 1); /* posStateMask */

        if (RangeDec_DecodeBit(rc, &probs[IsMatch(state, posState)]) == 0) {
            CLzmaProb* litProbs = &probs[LitProbs(state, prevByte)];
            if (state < kNumLitStates) {
                uint8_t symbol = 1;
                do { symbol = (symbol << 1) | RangeDec_DecodeBit(rc, &litProbs[symbol]); } while (symbol < 0x100);
                prevByte = (uint8_t)symbol;
            } else {
                uint8_t symbol = 1;
                do {
                    uint8_t bit = RangeDec_DecodeBit(rc, &litProbs[symbol]);
                    symbol = (symbol << 1) | bit;
                    if (RangeDec_DecodeBit(rc, &litProbs[0x100 + (rep0 >> 8) * 0x100 + symbol]) != bit)
                        symbol ^= 1;
                } while (symbol < 0x100);
                prevByte = (uint8_t)symbol;
            }
            dic[*dicPos] = prevByte;
            (*dicPos)++;
            state = state < 4 ? 0 : state - 3;
            continue;
        }

        uint32_t len;
        if (RangeDec_DecodeBit(rc, &probs[IsRep(state)]) == 0) {
            rep3 = rep2; rep2 = rep1; rep1 = rep0;
            len = DecodeLen(rc, &probs[RepLenCoder(state)], posState);
            state = state < 7 ? 7 : 10;

            uint32_t posSlot = RangeDec_DecodeDirectBits(rc, kNumPosSlotBits);
            if (posSlot < kStartPosModelIndex)
                rep0 = posSlot;
            else {
                uint32_t numDirectBits = (posSlot >> 1) - 1;
                rep0 = (2 | (posSlot & 1)) << numDirectBits;
                if (posSlot < kEndPosModelIndex)
                    rep0 += RangeDec_DecodeBit(rc, &probs[SpecPos(state, posSlot) + rep0 - posSlot - 1]);
                else {
                    rep0 += RangeDec_DecodeDirectBits(rc, numDirectBits - kNumAlignBits) << kNumAlignBits;
                    rep0 += RangeDec_DecodeBit(rc, &probs[AlignProbs(state) + rep0]);
                }
            }
            if (rep0 == 0xFFFFFFFF)
                return 0; /* End of stream */
            rep0++;
        } else {
            if (RangeDec_DecodeBit(rc, &probs[IsRepG0(state)]) == 0) {
                if (RangeDec_DecodeBit(rc, &probs[IsRep0Long(state, posState)]) == 0) {
                    state = state < 7 ? 9 : 11;
                    dic[*dicPos] = dic[*dicPos - rep0];
                    (*dicPos)++;
                    continue;
                }
            } else {
                uint32_t dist;
                if (RangeDec_DecodeBit(rc, &probs[IsRepG1(state)]) == 0)
                    dist = rep1;
                else {
                    if (RangeDec_DecodeBit(rc, &probs[IsRepG2(state)]) == 0)
                        dist = rep2;
                    else { dist = rep3; rep3 = rep2; }
                    rep2 = rep1;
                }
                rep1 = rep0; rep0 = dist;
            }
            len = DecodeLen(rc, &probs[RepLenCoder(state)], posState) + kMatchMinLen;
            state = state < 7 ? 8 : 11;
        }

        len += kMatchMinLen;
        do {
            dic[*dicPos] = dic[*dicPos - rep0];
            (*dicPos)++;
            len--;
        } while (len != 0 && *dicPos < dicBufSize);
    }
}

int32_t LzmaDec_Decompress(const uint8_t* compressed, uint32_t compressed_size,
                             uint8_t* output, uint32_t* output_size) {
    if (!compressed || !output || !output_size) return -1;
    if (compressed_size < RC_INIT_SIZE + 8) return -2;

    /* Parse header: [5 bytes props] [8 bytes original size LE] [data...] */
    const uint8_t* props = compressed;
    const uint8_t* data = compressed + RC_INIT_SIZE + 8;
    uint32_t data_size = compressed_size - RC_INIT_SIZE - 8;

    /* Read original size (8 bytes LE) */
    uint64_t origSize = 0;
    for (int i = 0; i < 8; i++)
        origSize |= (uint64_t)compressed[RC_INIT_SIZE + i] << (8 * i);

    if (origSize > *output_size) return -3;

    /* Validate properties byte */
    if (props[0] >= 9 * 5 * 5) return -4;

    /* Setup range decoder */
    CRangeDec rc;
    rc.buf = data;
    rc.bufPos = 0;
    rc.bufLimit = data_size;
    RangeDec_Init(&rc);

    /* Allocate probability table */
    CLzmaProb* probs = (CLzmaProb*)malloc(kNumProbs * sizeof(CLzmaProb));
    if (!probs) return -5;
    LzmaDec_InitProbs(probs);

    /* Decode */
    uint32_t dicPos = 0;
    uint32_t state = 0;
    uint32_t rep0 = 1, rep1 = 0, rep2 = 0, rep3 = 0;
    uint8_t prevByte = 0;

    /* Simple decode loop */
    while (dicPos < (uint32_t)origSize && rc.bufPos < rc.bufLimit) {
        uint32_t posState = dicPos & ((1 << (props[0] / (9 * 5))) - 1);

        if (RangeDec_DecodeBit(&rc, &probs[IsMatch(state, posState)]) == 0) {
            CLzmaProb* litProbs = &probs[LitProbs(state, prevByte)];
            uint8_t symbol = 1;
            if (state < kNumLitStates) {
                do { symbol = (symbol << 1) | RangeDec_DecodeBit(&rc, &litProbs[symbol]); } while (symbol < 0x100);
            } else {
                uint8_t byte2 = output[dicPos - rep0];
                do {
                    uint8_t bit = RangeDec_DecodeBit(&rc, &litProbs[symbol]);
                    symbol = (symbol << 1) | bit;
                    if (RangeDec_DecodeBit(&rc, &litProbs[0x100 + byte2 * 0x100 + symbol]) != bit)
                        symbol ^= 1;
                } while (symbol < 0x100);
            }
            prevByte = (uint8_t)symbol;
            output[dicPos++] = prevByte;
            state = state < 4 ? 0 : state - 3;
        } else {
            uint32_t len;
            if (RangeDec_DecodeBit(&rc, &probs[IsRep(state)]) == 0) {
                rep3 = rep2; rep2 = rep1; rep1 = rep0;
                len = DecodeLen(&rc, &probs[RepLenCoder(state)], posState);
                state = state < 7 ? 7 : 10;
                uint32_t posSlot = RangeDec_DecodeDirectBits(&rc, kNumPosSlotBits);
                if (posSlot < kStartPosModelIndex)
                    rep0 = posSlot;
                else {
                    uint32_t numDirectBits = (posSlot >> 1) - 1;
                    rep0 = (2 | (posSlot & 1)) << numDirectBits;
                    if (posSlot < kEndPosModelIndex)
                        rep0 += RangeDec_DecodeBit(&rc, &probs[SpecPos(state, posSlot) + rep0 - posSlot - 1]);
                    else {
                        rep0 += RangeDec_DecodeDirectBits(&rc, numDirectBits - kNumAlignBits) << kNumAlignBits;
                        rep0 += RangeDec_DecodeBit(&rc, &probs[AlignProbs(state) + rep0]);
                    }
                }
                if (rep0 == 0xFFFFFFFF) break;
                rep0++;
            } else {
                if (RangeDec_DecodeBit(&rc, &probs[IsRepG0(state)]) == 0) {
                    if (RangeDec_DecodeBit(&rc, &probs[IsRep0Long(state, posState)]) == 0) {
                        state = state < 7 ? 9 : 11;
                        output[dicPos] = output[dicPos - rep0];
                        dicPos++;
                        continue;
                    }
                } else {
                    uint32_t dist;
                    if (RangeDec_DecodeBit(&rc, &probs[IsRepG1(state)]) == 0)
                        dist = rep1;
                    else {
                        if (RangeDec_DecodeBit(&rc, &probs[IsRepG2(state)]) == 0)
                            dist = rep2;
                        else { dist = rep3; rep3 = rep2; }
                        rep2 = rep1;
                    }
                    rep1 = rep0; rep0 = dist;
                }
                len = DecodeLen(&rc, &probs[RepLenCoder(state)], posState) + kMatchMinLen;
                state = state < 7 ? 8 : 11;
            }
            len += kMatchMinLen;
            do {
                output[dicPos] = output[dicPos - rep0];
                dicPos++;
                len--;
            } while (len != 0 && dicPos < (uint32_t)origSize);
        }
    }

    *output_size = dicPos;
    free(probs);
    return 0;
}
