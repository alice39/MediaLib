#include "utils.h"
#include "../zlib/zlib.h"

#include <stdio.h>
#include <string.h>
#include <malloc.h>

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

void media_zlib_inflate(uint8_t* compressed, size_t compressed_size,
                        uint8_t** out_data, size_t* out_size) {
    z_stream stream;
    memset(&stream, 0, sizeof(z_stream));

    inflateInit(&stream);

    stream.next_in = compressed;
    stream.avail_in = compressed_size;

    uint8_t* data = malloc(0);
    size_t size = 0;

    size_t times = 1;
    do {
        size = 1024 * times;
        data = realloc(data, sizeof(uint8_t) * size);

        stream.avail_out = 1024;
        stream.next_out = data + 1024 * (times - 1);

        times++;

        inflate(&stream, Z_FINISH);
    } while (stream.avail_out == 0);

    // re-adjust
    size -= stream.avail_out;
    data = realloc(data, sizeof(uint8_t) * size);

    inflateEnd(&stream);

    *out_data = data;
    *out_size = size;
}

void media_zlib_deflate(uint8_t* data, size_t data_size,
                        uint8_t** out_compressed, size_t* out_size,
                        int compression_level) {
    if (compression_level < Z_DEFAULT_COMPRESSION) {
        compression_level = Z_DEFAULT_COMPRESSION;
    } else if (compression_level > Z_BEST_COMPRESSION) {
        compression_level = Z_BEST_COMPRESSION;
    }

    z_stream stream;
    memset(&stream, 0, sizeof(z_stream));

    deflateInit(&stream, compression_level);

    stream.next_in = data;
    stream.avail_in = data_size;

    uint8_t* compressed = malloc(0);
    size_t size = 0;

    size_t times = 1;
    do {
        size = 1024 * times;
        compressed = realloc(compressed, sizeof(uint8_t) * size);

        stream.avail_out = 1024;
        stream.next_out = compressed + 1024 * (times - 1);

        times++;

        deflate(&stream, Z_FINISH);
    } while (stream.avail_out == 0);

    // re-adjust
    size -= stream.avail_out;
    compressed = realloc(compressed, sizeof(uint8_t) * size);

    deflateEnd(&stream);

    *out_compressed = compressed;
    *out_size = size;
}
