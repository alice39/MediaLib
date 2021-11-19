#ifndef MEDIA_UTILS_GUARD_HEADER
#define MEDIA_UTILS_GUARD_HEADER

#include <stdint.h>
#include <stddef.h>

#define MEDIA_CRC32_DEFAULT 0xFFFFFFFF
#define MEDIA_CRC32(crc) (crc ^ 0xFFFFFFFF)

enum media_endian {
    MEDIA_LITTLE_ENDIAN,
    MEDIA_BIG_ENDIAN
};

uint32_t media_update_crc32(uint32_t crc, uint8_t* data, uint32_t size);

enum media_endian media_actual_endian();

void media_zlib_inflate(uint8_t* compressed, size_t compressed_size,
                        uint8_t** out_data, size_t* out_size);

void media_zlib_deflate(uint8_t* data, size_t data_size,
                        uint8_t** out_compressed, size_t* out_size,
                        int compression_level);

#endif // MEDIA_UTILS_GUARD_HEADER
