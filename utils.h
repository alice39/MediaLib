#ifndef MEDIA_UTILS_GUARD_HEADER
#define MEDIA_UTILS_GUARD_HEADER

#include <stdint.h>

#define MEDIA_CRC32_DEFAULT 0xFFFFFFFF
#define MEDIA_CRC32(crc) (crc ^ 0xFFFFFFFF)

enum media_endian {
    MEDIA_LITTLE_ENDIAN,
    MEDIA_BIG_ENDIAN
};

uint32_t media_update_crc32(uint32_t crc, uint8_t* data, uint32_t size);

enum media_endian media_actual_endian();

#endif // MEDIA_UTILS_GUARD_HEADER
