#ifndef MEDIA_UTILS_GUARD_HEADER
#define MEDIA_UTILS_GUARD_HEADER

#include <stdint.h>

#define MEDIA_CRC32_DEFAULT 0xFFFFFFFF
#define MEDIA_CRC32(crc) (crc ^ 0xFFFFFFFF)

uint32_t media_update_crc32(uint32_t crc, uint8_t* data, uint32_t size);

#endif // MEDIA_UTILS_GUARD_HEADER
