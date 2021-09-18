#include "utils.h"

#include <stdio.h>

static uint32_t CRCTable[256];
static uint8_t crc_table_initialized;

/* Make the table for a fast CRC. */
static void make_crc_table(void) {
    if (crc_table_initialized) {
        return;
    }

    for (uint32_t n = 0; n < 256; n++) {
        unsigned long c = (unsigned long) n;
        for (uint32_t k = 0; k < 8; k++) {
            if (c & 1) {
                c = 0xedb88320L ^ (c >> 1);
            } else {
                c = c >> 1;
            }
        }
        
        CRCTable[n] = c;
    }

    crc_table_initialized = 1;
}

uint32_t media_update_crc32(uint32_t crc, uint8_t* data, uint32_t size) {
    make_crc_table();

    for (uint32_t i = 0; i < size; i++) {
        uint8_t nLookupIndex = crc ^ data[i];
        crc = (crc >> 8) ^ CRCTable[nLookupIndex];
    }

    return crc;
}

enum media_endian media_actual_endian() {
    static enum media_endian endian = 0;
    static uint8_t initialized = 0;

    if (initialized == 0) {
        initialized = 1;

        uint32_t val = 0x01020304;
        switch (*((uint8_t*) &val)) {
            case 0x01: {
                endian = MEDIA_BIG_ENDIAN;
                break;
            }
            case 0x04: {
                endian = MEDIA_LITTLE_ENDIAN;
                break;
            }
            default: {
                perror("It could not handle endianess");
            }
        }
    }

    return endian;
}
