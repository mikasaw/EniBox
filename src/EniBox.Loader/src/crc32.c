#include "crc32.h"
static uint32_t crc32_table[256];
static int crc32_table_initialized = 0;
static void CRC32_InitTable(void) {
    const uint32_t polynomial = 0xEDB88320;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++)
            crc = (crc & 1) ? (crc >> 1) ^ polynomial : (crc >> 1);
        crc32_table[i] = crc;
    }
    crc32_table_initialized = 1;
}
uint32_t CRC32_Compute(const uint8_t* data, size_t length) {
    if (!crc32_table_initialized) CRC32_InitTable();
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++)
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFF;
}
