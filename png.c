#include "image.h"
#include "lib/zlib.h"
#include "utils.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char PNG_FILE_HEADER[9] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00};
static const uint8_t PNG_BITS_TYPE[7][17] = {
    {0, 8, 8, 0, 8, 0, 0, 0,  8, 0, 0, 0, 0, 0, 0, 0, 16},
    {0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,  0},
    {0, 0, 0, 0, 0, 0, 0, 0, 24, 0, 0, 0, 0, 0, 0, 0, 48},
    {0, 8, 8, 0, 8, 0, 0, 0,  8, 0, 0, 0, 0, 0, 0, 0,  0},
    {0, 0, 0, 0, 0, 0, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0, 32},
    {0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,  0},
    {0, 0, 0, 0, 0, 0, 0, 0, 32, 0, 0, 0, 0, 0, 0, 0, 64}
};

struct image_png_chunk {
    uint32_t length;
    char type[5];
    uint8_t* data;
    uint32_t crc;
};

struct image_png {
    uint32_t size;
    struct image_png_chunk* chunks;
};

struct image_png_chunk_IHDR {
    uint32_t width;
    uint32_t height;
    uint8_t depth;
    uint8_t color;
    uint8_t filter;
    uint8_t compression;
    uint8_t interlace;
};

struct image_png_chunk_IDAT {
    uint32_t size;
    uint8_t* pixels;
};

static inline uint32_t convert_int_be(uint32_t value);

static void _png_find_chunks(struct image_png* image, char* type, uint32_t** pchunk_indexes, uint32_t* size);
// return 0 if success, otherwise another number
// ihdr can be null, just checking if chunk is an IHDR valid
static int _png_read_chunk_IHDR(struct image_png_chunk* chunk, struct image_png_chunk_IHDR* ihdr);
static int _png_read_chunk_IDAT(struct image_png_chunk* chunk, struct image_png_chunk_IDAT* idat);

static void _png_write_chunk_IHDR(struct image_png_chunk_IHDR* ihdr, struct image_png_chunk* chunk);
static void _png_write_chunk_IDAT(struct image_png_chunk_IDAT* idat, struct image_png_chunk* chunk);

typedef void (*_png_pixel_fn)(struct image_png_chunk_IHDR*, void*, struct image_color*);

static void _png_execute_pixel(struct image_png* image, uint32_t x, uint32_t y,
                               uint8_t readOnly, _png_pixel_fn action, struct image_color* color);
static void _png_get_pixel(struct image_png_chunk_IHDR* ihdr, void* pixel, struct image_color* color);
static void _png_set_pixel(struct image_png_chunk_IHDR* ihdr, void* pixel, struct image_color* color);

struct image_png* image_png_create(enum image_color_type type, uint32_t width, uint32_t height) {
    struct image_png* image = malloc(sizeof(struct image_png));
    image->size = 3;
    image->chunks = malloc(sizeof(struct image_png_chunk) * 3);

    struct image_png_chunk_IHDR ihdr;
    ihdr.width = width;
    ihdr.height = height;
    ihdr.depth = (type & 0x40) != 0 ? 16 : 8;

    switch (type & 0x7F) {
        case IMAGE_RGBA8_COLOR:
        case IMAGE_RGBA16_COLOR: {
            ihdr.color = (type & IMAGE_ALPHA_BIT) != 0 ? 6 : 2;
            break;
        }
        case IMAGE_GRAY8_COLOR:
        case IMAGE_GRAY16_COLOR: {
            ihdr.color = (type & IMAGE_ALPHA_BIT) != 0 ? 4 : 0;
            break;
        }
        case IMAGE_INDEXED_COLOR: {
            ihdr.color = 3;
            break;
        }
    }

    ihdr.compression = 0;
    ihdr.filter = 0;
    ihdr.interlace = 0;

    _png_write_chunk_IHDR(&ihdr, &image->chunks[0]);

    struct image_png_chunk_IDAT idat;
    uint32_t pixel_size = PNG_BITS_TYPE[ihdr.color][ihdr.depth] / 8;
    idat.size = (width * height) * pixel_size + height;
    idat.pixels = malloc(sizeof(uint8_t) * idat.size);
    memset(idat.pixels, 0, sizeof(uint8_t) * idat.size);

    _png_write_chunk_IDAT(&idat, &image->chunks[1]);

    struct image_png_chunk* iend = &image->chunks[2];
    iend->length = 0;
    strcpy(iend->type, "IEND");
    iend->data = NULL;
    iend->crc = MEDIA_CRC32(media_update_crc32(MEDIA_CRC32_DEFAULT, (uint8_t*) "IEND", 4));

    return image;
}

struct image_png* image_png_open(const char* path) {
    FILE* file = fopen(path, "r");
    if (file == NULL) {
        return NULL;
    }

    // check file header
    char header[9] = {0};
    fread(header, sizeof(char), 8, file);
    if (strcmp(header, PNG_FILE_HEADER) != 0) {
        // file header is not png file header

        fclose(file);
        return NULL;
    }

    struct image_png* image = malloc(sizeof(struct image_png));
    
    image->size = 0;
    image->chunks = malloc(0);

    struct image_png_chunk chunk;
    do {
        if (feof(file)) {
            // Error, IEND wasn't found!

            image_png_close(image);
            return NULL;
        }

        memset(&chunk, 0, sizeof(struct image_png_chunk));

        fread(&chunk.length, sizeof(uint32_t), 1, file);
        chunk.length = convert_int_be(chunk.length);

        fread(chunk.type, sizeof(char), 4, file);

        chunk.data = malloc(sizeof(uint8_t) * chunk.length);
        fread(chunk.data, sizeof(uint8_t), chunk.length, file);

        fread(&chunk.crc, sizeof(uint32_t), 1, file);
        chunk.crc = convert_int_be(chunk.crc);

        printf("Chunk: %d %s\n", chunk.length, chunk.type);

        image->size++;
        image->chunks = realloc(image->chunks, sizeof(struct image_png_chunk) * image->size);
        memcpy(&image->chunks[image->size - 1], &chunk, sizeof(struct image_png_chunk));
    } while (strcmp(chunk.type, "IEND") != 0);

    fclose(file);

    if (_png_read_chunk_IHDR(&image->chunks[0], NULL) != 0) {
        // IHDR is invalid or it's not the first chunk

        image_png_close(image);
        image = NULL;
    }

    return image;
}

uint32_t image_png_get_width(struct image_png* image) {
    struct image_png_chunk_IHDR ihdr;
    _png_read_chunk_IHDR(&image->chunks[0], &ihdr);
    return ihdr.width;
}

uint32_t image_png_get_height(struct image_png* image) {
    struct image_png_chunk_IHDR ihdr;
    _png_read_chunk_IHDR(&image->chunks[0], &ihdr);
    return ihdr.height;
}

uint8_t image_png_get_color(struct image_png* image) {
    struct image_png_chunk_IHDR ihdr;
    _png_read_chunk_IHDR(&image->chunks[0], &ihdr);
    return ihdr.color;
}

uint8_t image_png_get_depth(struct image_png* image) {
    struct image_png_chunk_IHDR ihdr;
    _png_read_chunk_IHDR(&image->chunks[0], &ihdr);
    return ihdr.depth;
}

void image_png_get_pixel(struct image_png* image, uint32_t x, uint32_t y, struct image_color* color) {
    _png_execute_pixel(image, x, y, 1, _png_get_pixel, color);
}

void image_png_set_pixel(struct image_png* image, uint32_t x, uint32_t y, struct image_color color) {
    _png_execute_pixel(image, x, y, 0, _png_set_pixel, &color);
}

void image_png_save(struct image_png* image, const char* path) {
    FILE* file = fopen(path, "w+");
    if (file == NULL) {
        // Error, it could not create or truct file
        return;
    }

    fwrite(PNG_FILE_HEADER, sizeof(uint8_t), 8, file);

    for (uint32_t i = 0; i < image->size; i++) {
        struct image_png_chunk* chunk = &image->chunks[i];

        chunk->length = convert_int_be(chunk->length);
        fwrite(&chunk->length, sizeof(uint32_t), 1, file);
        chunk->length = convert_int_be(chunk->length);

        fwrite(chunk->type, sizeof(uint8_t), 4, file);
        fwrite(chunk->data, sizeof(uint8_t), chunk->length, file);

        chunk->crc = convert_int_be(chunk->crc);
        fwrite(&chunk->crc, sizeof(uint32_t), 1, file);
        chunk->crc = convert_int_be(chunk->crc);
    }

    fclose(file);
}

void image_png_close(struct image_png* image) {
    for (uint32_t i = 0; i < image->size; i++) {
        free(image->chunks[i].data);
    }

    free(image->chunks);
    free(image);
}

static inline uint32_t convert_int_be(uint32_t value) {
    if (media_actual_endian() == MEDIA_BIG_ENDIAN) {
        // no need to shift
        return value;
    }

    uint8_t* bytes = (uint8_t *) &value;
    return bytes[0] << 24 | bytes[1] << 16 | bytes[2] << 8 | bytes[3];
}

static void _png_find_chunks(struct image_png* image, char* type, uint32_t** pchunk_indexes, uint32_t* psize) {
    if (image == NULL || type == NULL || pchunk_indexes == NULL || psize == NULL) {
        return;
    }

    uint32_t size = 0;
    uint32_t* chunk_indexes = malloc(0);

    for (uint32_t i = 0; i < image->size; i++) {
        if (strcmp(type, image->chunks[i].type) != 0) {
            continue;
        }

        size++;
        chunk_indexes = realloc(chunk_indexes, sizeof(uint32_t) * size);
        chunk_indexes[size - 1] = i;
    }

    *psize = size;
    *pchunk_indexes = chunk_indexes;
}

static int _png_read_chunk_IHDR(struct image_png_chunk* chunk, struct image_png_chunk_IHDR* ihdr) {
    if (chunk == NULL || chunk->length != 13 || strcmp(chunk->type, "IHDR") != 0) {
        return 1;
    }

    struct image_png_chunk_IHDR def_ihdr;
    if (ihdr == NULL) {
        ihdr = &def_ihdr;
    }

    memcpy(ihdr, chunk->data, 13);
    ihdr->width = convert_int_be(ihdr->width);
    ihdr->height = convert_int_be(ihdr->height);

    return 0;
}

static int _png_read_chunk_IDAT(struct image_png_chunk* chunk, struct image_png_chunk_IDAT* idat) {
    if (chunk == NULL || strcmp(chunk->type, "IDAT") != 0) {
        return 1;
    }

    if (idat == NULL) {
        return 0;
    }

    idat->size = 0;
    idat->pixels = malloc(0);

    z_stream stream;
    memset(&stream, 0, sizeof(z_stream));

    inflateInit(&stream);

    stream.avail_in = chunk->length;
    stream.next_in = chunk->data;

    uint32_t times = 1;
    do {
        idat->size = 1024 * times;
        idat->pixels = realloc(idat->pixels, sizeof(uint8_t) * idat->size);

        stream.avail_out = 1024;
        stream.next_out = idat->pixels + 1024 * (times - 1);

        times++;

        inflate(&stream, Z_NO_FLUSH);
    } while (stream.avail_out == 0);

    // re-adjust
    idat->size -= stream.avail_out;
    idat->pixels = realloc(idat->pixels, sizeof(uint8_t) * idat->size);

    inflateEnd(&stream);

    return 0;
}

static void _png_write_chunk_IHDR(struct image_png_chunk_IHDR* ihdr, struct image_png_chunk* chunk) {
    chunk->length = 13;
    strcpy(chunk->type, "IHDR");
    chunk->data = malloc(sizeof(uint8_t) * 13);

    ihdr->width = convert_int_be(ihdr->width);
    ihdr->height = convert_int_be(ihdr->height);

    memcpy(chunk->data, ihdr, sizeof(uint8_t) * 13);

    ihdr->width = convert_int_be(ihdr->width);
    ihdr->height = convert_int_be(ihdr->height);

    uint32_t crc = MEDIA_CRC32_DEFAULT;
    crc = media_update_crc32(crc, (uint8_t *) "IHDR", 4);
    crc = media_update_crc32(crc, chunk->data, chunk->length);

    chunk->crc = MEDIA_CRC32(crc);
}

static void _png_write_chunk_IDAT(struct image_png_chunk_IDAT* idat, struct image_png_chunk* chunk) {
    chunk->length = 0;
    strcpy(chunk->type, "IDAT");
    chunk->data = malloc(0);

    z_stream stream;
    memset(&stream, 0, sizeof(z_stream));

    // deflateInit(&stream, Z_DEFAULT_COMPRESSION);
    deflateInit(&stream, 9);

    stream.avail_in = idat->size;
    stream.next_in = idat->pixels;

    // fetch until available output size be 0
    uint16_t times = 1;
    do {
        chunk->length = 1024 * times;
        chunk->data = realloc(chunk->data, sizeof(uint8_t) * chunk->length);

        stream.avail_out = 1024;
        stream.next_out = chunk->data + 1024 * (times - 1);

        times++;

        deflate(&stream, Z_FINISH);
    } while (stream.avail_out == 0);

    // re-adjust
    chunk->length -= stream.avail_out;
    chunk->data = realloc(chunk->data, sizeof(uint8_t) * chunk->length);

    deflateEnd(&stream);

    uint32_t crc = MEDIA_CRC32_DEFAULT;
    crc = media_update_crc32(crc, (uint8_t *) "IDAT", 4);
    crc = media_update_crc32(crc, chunk->data, chunk->length);

    chunk->crc = MEDIA_CRC32(crc);
}

static void _png_execute_pixel(struct image_png* image, uint32_t x, uint32_t y,
                               uint8_t readOnly, _png_pixel_fn action, struct image_color* color) {
    struct image_png_chunk_IHDR ihdr;
    int ihdr_ret = _png_read_chunk_IHDR(&image->chunks[0], &ihdr);
    if (ihdr_ret != 0) {
        perror("Error reading IHDR");
        return;
    }

    uint32_t size;
    uint32_t* chunk_indexes;
    _png_find_chunks(image, "IDAT", &chunk_indexes, &size);

    if (size == 0) {
        perror("Error, no IDAT chunks found");
        free(chunk_indexes);
        return;
    }

    uint8_t type = ihdr.color;
    uint8_t depth = ihdr.depth;

    uint32_t pixel_size = PNG_BITS_TYPE[type][depth] / 8;
    uint64_t pixel_index = (x + y * ihdr.width) * pixel_size + y + 1;

    uint64_t idat_total_size = 0;
 
    for (uint32_t i = 0; i < size; i++) {
        uint32_t chunk_index = chunk_indexes[i];
        struct image_png_chunk* chunk = &image->chunks[chunk_index];

        struct image_png_chunk_IDAT idat;
        int idat_ret = _png_read_chunk_IDAT(chunk, &idat);
        if (idat_ret != 0) {
            perror("Error reading a IDAT chunk");
            continue;
        }

        // probably pixel is in another IDAT chunk
        idat_total_size += idat.size;
        if (idat_total_size <= pixel_index) {
            continue;
        }

        action(&ihdr, &idat.pixels[pixel_index], color);

        // if it should save pixel
        if (readOnly == 0) {
            _png_write_chunk_IDAT(&idat, chunk);
        }

        break;
    }

    free(chunk_indexes);
}

static void _png_get_pixel(struct image_png_chunk_IHDR* ihdr, void* pixel, struct image_color* color) {
    uint8_t* pixel_8bits = (uint8_t *) pixel;
    uint16_t* pixel_16bits = (uint16_t *) pixel;

    uint8_t depth = ihdr->depth;

    switch (ihdr->color) {
        case 0:
        case 4: {
            if (depth <= 8) {
                color->type = IMAGE_GRAY8_COLOR;
                color->ga8.gray = pixel_8bits[0];

                // if alpha is present
                if (ihdr->color == 4) {
                    color->ga8.alpha = pixel_8bits[1];
                }
            } else if (depth == 16) {
                color->type = IMAGE_GRAY16_COLOR;
                color->ga16.gray = pixel_16bits[0];

                // if alpha is present
                if (ihdr->color == 4) {
                    color->ga16.alpha = pixel_16bits[1];
                }
            }

            break;
        }
        case 2:
        case 6: {
            if (depth == 8) {
                color->type = IMAGE_RGBA8_COLOR;
                color->rgba8.red = pixel_8bits[0];
                color->rgba8.green = pixel_8bits[1];
                color->rgba8.blue = pixel_8bits[2];

                // if alpha is present
                if (ihdr->color == 6) {
                    color->rgba8.alpha = pixel_8bits[3];
                }
            } else if (depth == 16) {
                color->type = IMAGE_RGBA16_COLOR;
                color->rgba16.red = pixel_16bits[0];
                color->rgba16.green = pixel_16bits[1];
                color->rgba16.blue = pixel_16bits[2];

                // if alpha is present
                if (ihdr->color == 6) {
                    color->rgba16.alpha = pixel_16bits[3];
                }
            }

            break;
        }
        case 3: {
            color->type = IMAGE_INDEXED_COLOR;
            color->indexed = pixel_8bits[0];

            break;
        }
    }
}

static void _png_set_pixel(struct image_png_chunk_IHDR* ihdr, void* pixel, struct image_color* color) {
    uint8_t* pixel_8bits = (uint8_t *) pixel;
    uint16_t* pixel_16bits = (uint16_t *) pixel;

    uint8_t depth = ihdr->depth;

    switch (ihdr->color) {
        case 0:
        case 4: {
            if (depth <= 8) {
                pixel_8bits[0] = color->ga8.gray;

                // if alpha is present
                if (ihdr->color == 4) {
                    pixel_8bits[1] = color->ga8.alpha;
                }
            } else if (depth == 16) {
                pixel_16bits[0] = color->ga8.gray;

                // if alpha is present
                if (ihdr->color == 4) {
                    pixel_16bits[1] = color->ga16.alpha;
                }
            }

            break;
        }
        case 2:
        case 6: {
            if (depth == 8) {
                pixel_8bits[0] = color->rgba8.red;
                pixel_8bits[1] = color->rgba8.green;
                pixel_8bits[2] = color->rgba8.blue;

                // if alpha is present
                if (ihdr->color == 6) {
                    pixel_8bits[3] = color->rgba8.alpha;
                }
            } else if (depth == 16) {
                pixel_16bits[0] = color->rgba16.red;
                pixel_16bits[1] = color->rgba16.green;
                pixel_16bits[2] = color->rgba16.blue;

                // if alpha is present
                if (ihdr->color == 6) {
                    pixel_16bits[3] = color->rgba16.alpha;
                }
            }

            break;
        }
        case 3: {
            pixel_8bits[0] = color->indexed;

            break;
        }
    }
}
