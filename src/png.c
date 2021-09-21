#include "image.h"
#include "../zlib/zlib.h"
#include "utils.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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

    uint32_t location;
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

enum image_png_idat_type {
    PNG_IDAT_SCANLINES,
    PNG_IDAT_PIXELS
};

struct image_png_chunk_IDAT {
    enum image_png_idat_type type;
    uint32_t size;
    uint8_t* data;

    uint32_t location;
};

struct image_png {
    struct image_png_chunk_IHDR ihdr;
    // IDAT needs to be in PIXELS instead of SCANLINES
    struct image_png_chunk_IDAT idat;

    uint32_t unknown_size;
    struct image_png_chunk* unknown_chunks;
};

static inline uint32_t convert_int_be(uint32_t value);
static void _png_convert_color(struct image_color* color, enum image_color_type type);

static void _png_copy_chunk(struct image_png_chunk* src, struct image_png_chunk* dest);

// return 0 if success, otherwise another number
// ihdr can be null, just checking if chunk is an IHDR valid
static int _png_read_chunk_IHDR(struct image_png_chunk* chunk, struct image_png_chunk_IHDR* ihdr);
// this is only based in zlib and it'll write in IDAT chunk as SCANLINES
static int _png_read_chunk_IDAT(struct image_png_chunk* chunk, struct image_png_chunk_IDAT* idat);

static void _png_write_chunk_IHDR(struct image_png_chunk_IHDR* ihdr, struct image_png_chunk* chunk);
// this is only based in zlib, also it needs that IDAT chunk be in SCANLINES
static void _png_write_chunk_IDAT(struct image_png_chunk_IDAT* idat, struct image_png_chunk* chunk);

static void _png_convert_chunk_IDAT(struct image_png_chunk_IHDR* ihdr,
                                    struct image_png_chunk_IDAT* idat,
                                    enum image_png_idat_type type);

// just if IDAT chunk is not used anymore
static inline void _png_free_chunk_IDAT(struct image_png_chunk_IDAT* idat);

typedef void (*_png_pixel_fn)(struct image_png_chunk_IHDR*, void*, struct image_color*);

static void _png_execute_pixel(struct image_png* image, uint32_t x, uint32_t y,
                               _png_pixel_fn action, struct image_color* color);
static void _png_get_pixel(struct image_png_chunk_IHDR* ihdr, void* pixel, struct image_color* color);
static void _png_set_pixel(struct image_png_chunk_IHDR* ihdr, void* pixel, struct image_color* color);

struct image_png* image_png_create(enum image_color_type type, uint32_t width, uint32_t height) {
    struct image_png* image = malloc(sizeof(struct image_png));

    struct image_png_chunk_IHDR* ihdr = &image->ihdr;
    ihdr->width = width;
    ihdr->height = height;
    ihdr->depth = (type & 0x40) != 0 ? 16 : 8;

    switch (IMAGE_IGNORE_ALPHA(type)) {
        case IMAGE_RGBA8_COLOR:
        case IMAGE_RGBA16_COLOR: {
            ihdr->color = (type & IMAGE_ALPHA_BIT) != 0 ? 6 : 2;
            break;
        }
        case IMAGE_GRAY8_COLOR:
        case IMAGE_GRAY16_COLOR: {
            ihdr->color = (type & IMAGE_ALPHA_BIT) != 0 ? 4 : 0;
            break;
        }
        case IMAGE_INDEXED_COLOR: {
            ihdr->color = 3;
            break;
        }
    }

    ihdr->compression = 0;
    ihdr->filter = 0;
    ihdr->interlace = 0;

    struct image_png_chunk_IDAT* idat = &image->idat;
    idat->type = PNG_IDAT_PIXELS;
    uint32_t pixel_size = PNG_BITS_TYPE[ihdr->color][ihdr->depth] / 8;
    idat->size = width * height * pixel_size;
    idat->data = malloc(sizeof(uint8_t) * idat->size);
    memset(idat->data, 0, sizeof(uint8_t) * idat->size);
    idat->location = 1;

    image->unknown_size = 0;
    image->unknown_chunks = NULL;

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
    image->idat.data = NULL;
    image->unknown_size = 0;
    image->unknown_chunks = NULL;

    struct image_png_chunk idat_chunk;
    memset(&idat_chunk, 0, sizeof(struct image_png_chunk));

    uint32_t location = 0;
    struct image_png_chunk chunk;
    do {
        if (feof(file)) {
            // Error, IEND wasn't found!

            image_png_close(image);
            image = NULL;
            break;
        }

        memset(&chunk, 0, sizeof(struct image_png_chunk));

        fread(&chunk.length, sizeof(uint32_t), 1, file);
        chunk.length = convert_int_be(chunk.length);

        fread(chunk.type, sizeof(char), 4, file);

        chunk.data = malloc(sizeof(uint8_t) * chunk.length);
        fread(chunk.data, sizeof(uint8_t), chunk.length, file);

        fread(&chunk.crc, sizeof(uint32_t), 1, file);
        chunk.crc = convert_int_be(chunk.crc);

        chunk.location = location++;

        printf("Chunk: %d %s\n", chunk.length, chunk.type);

        if (strcmp(chunk.type, "IEND") == 0) {
            free(chunk.data);
            break;
        }

        if (strcmp(chunk.type, "IHDR") == 0) {
            int ihdr_ret = _png_read_chunk_IHDR(&chunk, &image->ihdr);
            free(chunk.data);

            if (chunk.location != 0 || ihdr_ret != 0 || image->ihdr.compression != 0) {
                // IHDR is invalid, it's not the first chunk or compression is not zlib

                image_png_close(image);
                image = NULL;
                break;
            }
        } else if (strcmp(chunk.type, "IDAT") == 0) {
            if (idat_chunk.length > 0) {

                uint32_t old_size = idat_chunk.length;

                idat_chunk.length += chunk.length;
                idat_chunk.data = realloc(idat_chunk.data, sizeof(uint8_t) * idat_chunk.length);

                memcpy(idat_chunk.data + old_size, chunk.data, chunk.length);
            } else {
                _png_copy_chunk(&chunk, &idat_chunk);
            }

            free(chunk.data);

            if (location == 0) {
                // IDAT is before than IHDR

                image_png_close(image);
                image = NULL;
                break;
            }
        } else {
            image->unknown_size++;
            image->unknown_chunks = realloc(image->unknown_chunks, sizeof(struct image_png_chunk) * image->unknown_size);
            memcpy(&image->unknown_chunks[image->unknown_size - 1], &chunk, sizeof(struct image_png_chunk));
        }
    } while (1);

    if (image != NULL) {
        _png_read_chunk_IDAT(&idat_chunk, &image->idat);
        _png_convert_chunk_IDAT(&image->ihdr, &image->idat, PNG_IDAT_PIXELS);
    }

    fclose(file);

    return image;
}

void image_png_get_dimension(struct image_png* image, struct image_dimension* dimension) {
    dimension->width = image->ihdr.width;
    dimension->height = image->ihdr.height;
}

int image_png_set_dimension(struct image_png* image, struct image_dimension dimension) {
    image->ihdr.width = dimension.width;
    image->ihdr.height = dimension.height;

    uint32_t old_size = image->idat.size;
    uint8_t* old_pixels = image->idat.data;

    uint32_t pixel_size = PNG_BITS_TYPE[image->ihdr.color][image->ihdr.depth] / 8;
    image->idat.size = dimension.width * dimension.height * pixel_size;
    image->idat.data = realloc(image->idat.data, sizeof(uint8_t) * image->idat.size);

    // if realloc fails
    if (image->idat.data == NULL) {
        image->idat.size = old_size;
        image->idat.data = old_pixels;
        return 1;
    }

    return 0;
}

void image_png_get_color(struct image_png* image, enum image_color_type* type) {
    uint8_t color = image->ihdr.color;
    uint8_t depth = image->ihdr.depth;

    switch (color) {
        case 0:
        case 4: {
            if (depth <= 8) {
                *type = IMAGE_GRAY8_COLOR;
            } else if (depth == 16) {
                *type = IMAGE_GRAY16_COLOR;
            }

            if (color == 4) {
                *type |= IMAGE_ALPHA_BIT;
            }

            break;
        }
        case 2:
        case 6: {
            if (depth == 8) {
                *type = IMAGE_RGBA8_COLOR;
            } else if (depth == 16) {
                *type = IMAGE_RGBA16_COLOR;
            }

            if (color == 6) {
                *type |= IMAGE_ALPHA_BIT;
            }

            break;
        }
        case 3: {
            *type = IMAGE_INDEXED_COLOR;

            break;
        }
    }
}

void image_png_set_color(struct image_png* image, enum image_color_type type) {
    struct image_png_chunk_IHDR* ihdr = &image->ihdr;
    struct image_png_chunk_IDAT* idat = &image->idat;

    uint8_t color = 0;
    uint8_t depth = image_get_depth(type);

    switch (IMAGE_IGNORE_ALPHA(type)) {
        case IMAGE_RGBA8_COLOR:
        case IMAGE_RGBA16_COLOR: {
            color = (type & IMAGE_ALPHA_BIT) != 0 ? 6 : 2;
            break;
        }
        case IMAGE_GRAY8_COLOR:
        case IMAGE_GRAY16_COLOR: {
            color = (type & IMAGE_ALPHA_BIT) != 0 ? 4 : 0;
            break;
        }
        case IMAGE_INDEXED_COLOR: {
            color = 3;
            break;
        }
    }

    // it didn't change anything
    if (color == ihdr->color && depth == ihdr->depth) {
        return;
    }

    // now do a copy of actual pixel colors to convert them
    // to the new type

    struct image_color* color_pixels = malloc(sizeof(struct image_color) * ihdr->width * ihdr->height);
    for (uint32_t y = 0; y < ihdr->height; y++) {
        for (uint32_t x = 0; x < ihdr->width; x++) {
            image_png_get_pixel(image, x, y, &color_pixels[x + y * ihdr->width]);
        }
    }

    ihdr->color = color;
    ihdr->depth = depth;

    uint32_t pixel_size = PNG_BITS_TYPE[ihdr->color][ihdr->depth];
    idat->size = ihdr->width * ihdr->height * pixel_size;
    idat->data = realloc(idat->data, sizeof(uint8_t) * idat->size);

    for (uint32_t y = 0; y < ihdr->height; y++) {
        for (uint32_t x = 0; x < ihdr->width; x++) {
            struct image_color color_pixel = color_pixels[x + y * ihdr->width];
            _png_convert_color(&color_pixel, type);
            image_png_set_pixel(image, x, y, color_pixel);
        }
    }

    free(color_pixels);
}

void image_png_get_pixel(struct image_png* image, uint32_t x, uint32_t y, struct image_color* color) {
    _png_execute_pixel(image, x, y, _png_get_pixel, color);
}

void image_png_set_pixel(struct image_png* image, uint32_t x, uint32_t y, struct image_color color) {
    _png_execute_pixel(image, x, y, _png_set_pixel, &color);
}

struct image_png* image_png_copy(struct image_png* image) {
    struct image_png* copy_image = malloc(sizeof(struct image_png));

    if (copy_image != NULL) {
        memcpy(&copy_image->ihdr, &image->ihdr, sizeof(struct image_png_chunk_IHDR));

        copy_image->idat.type = image->idat.type;
        copy_image->idat.size = image->idat.size;
        copy_image->idat.data = malloc(sizeof(uint8_t) * copy_image->idat.size);
        memcpy(copy_image->idat.data, image->idat.data, sizeof(uint8_t) * copy_image->idat.size);
        copy_image->idat.location = image->idat.location;

        copy_image->unknown_size = image->unknown_size;
        copy_image->unknown_chunks = malloc(sizeof(struct image_png_chunk) * copy_image->unknown_size);
        for (uint32_t i = 0; i < copy_image->unknown_size; i++) {
            _png_copy_chunk(&image->unknown_chunks[i], &copy_image->unknown_chunks[i]);
        }
    }

    return copy_image;
}

void image_png_tobytes(struct image_png* image, uint8_t** pbytes, uint32_t* psize) {
    uint32_t size = 8;
    uint8_t* bytes = malloc(sizeof(uint8_t) * 8);

    memcpy(bytes, PNG_FILE_HEADER, sizeof(uint8_t) * 8);

    uint32_t chunk_size = 3 + image->unknown_size;
    struct image_png_chunk* chunks = malloc(chunk_size * sizeof(struct image_png_chunk));
    memset(chunks, 0, sizeof(struct image_png_chunk) * chunk_size);

    _png_write_chunk_IHDR(&image->ihdr, &chunks[0]);

    _png_convert_chunk_IDAT(&image->ihdr, &image->idat, PNG_IDAT_SCANLINES);
    _png_write_chunk_IDAT(&image->idat, &chunks[image->idat.location]);
    _png_convert_chunk_IDAT(&image->ihdr, &image->idat, PNG_IDAT_PIXELS);

    struct image_png_chunk* iend = &chunks[2 + image->unknown_size];
    iend->length = 0;
    strcpy(iend->type, "IEND");
    iend->data = NULL;
    iend->crc = MEDIA_CRC32(media_update_crc32(MEDIA_CRC32_DEFAULT, (uint8_t*) "IEND", 4));

    for (uint32_t i = 0; i < image->unknown_size; i++) {
        struct image_png_chunk* chunk = &image->unknown_chunks[i];
        _png_copy_chunk(chunk, &chunks[chunk->location]);
    }

    for (uint32_t i = 0; i < chunk_size; i++) {
        struct image_png_chunk* chunk = &chunks[i];

        chunk->length = convert_int_be(chunk->length);
        size += sizeof(uint32_t);
        bytes = realloc(bytes, sizeof(uint8_t) * size);
        memcpy(bytes + size - sizeof(uint32_t), &chunk->length, sizeof(uint32_t));
        chunk->length = convert_int_be(chunk->length);

        size += sizeof(uint8_t) * 4;
        bytes = realloc(bytes, sizeof(uint8_t) * size);
        memcpy(bytes + size - sizeof(uint8_t) * 4, chunk->type, sizeof(uint8_t) * 4);

        size += sizeof(uint8_t) * chunk->length;
        bytes = realloc(bytes, sizeof(uint8_t) * size);
        memcpy(bytes + size - sizeof(uint8_t) * chunk->length, chunk->data, sizeof(uint8_t) * chunk->length);
 
        chunk->crc = convert_int_be(chunk->crc);
        size += sizeof(uint32_t);
        bytes = realloc(bytes, sizeof(uint8_t) * size);
        memcpy(bytes + size - sizeof(uint32_t), &chunk->crc, sizeof(uint32_t));
        chunk->crc = convert_int_be(chunk->crc);

        free(chunk->data);
    }

    free(chunks);

    *pbytes = bytes;
    *psize = size;
}

void image_png_save(struct image_png* image, const char* path) {
    FILE* file = fopen(path, "w+");
    if (file == NULL) {
        // Error, it could not create or truct file
        return;
    }

    uint32_t size;
    uint8_t* bytes;
    image_png_tobytes(image, &bytes, &size);

    fwrite(bytes, sizeof(uint8_t), size, file);
    fclose(file);

    free(bytes);
}

void image_png_close(struct image_png* image) {
    free(image->idat.data);

    for (uint32_t i = 0; i < image->unknown_size; i++) {
        free(image->unknown_chunks[i].data);
    }

    free(image->unknown_chunks);

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

inline static uint16_t twice16(uint8_t value) {
    return (value << 8) | value;
}

static void _png_convert_color(struct image_color* color, enum image_color_type type) {
    if (color->type == type) {
        return;
    }

    struct image_color new_color;
    new_color.type = type;

    switch (IMAGE_IGNORE_ALPHA(color->type)) {
        case IMAGE_RGBA8_COLOR:
        case IMAGE_RGBA16_COLOR: {
            switch (IMAGE_IGNORE_ALPHA(type)) {
                case IMAGE_RGBA8_COLOR: {
                    if (IMAGE_IGNORE_ALPHA(color->type) == IMAGE_RGBA16_COLOR) {
                        new_color.rgba8.red = color->rgba16.red & 0xFF;
                        new_color.rgba8.green = color->rgba16.green & 0xFF;
                        new_color.rgba8.blue = color->rgba16.blue & 0xFF;
                        new_color.rgba8.alpha = color->rgba16.alpha & 0xFF;
                    }

                    break;
                }
                case IMAGE_RGBA16_COLOR: {
                    if (IMAGE_IGNORE_ALPHA(color->type) == IMAGE_RGBA8_COLOR) {
                        new_color.rgba16.red = twice16(color->rgba8.red);
                        new_color.rgba16.green = twice16(color->rgba8.green);
                        new_color.rgba16.blue = twice16(color->rgba8.blue);
                        new_color.rgba16.alpha = twice16(color->rgba8.alpha);
                    }

                    break;
                }
                case IMAGE_GRAY8_COLOR: {
                    if (IMAGE_IGNORE_ALPHA(color->type) == IMAGE_RGBA8_COLOR) {
                        new_color.ga8.gray = (color->rgba8.red + color->rgba8.green + color->rgba8.blue) / 3;
                        new_color.ga8.alpha = color->rgba8.alpha;
                    } else if (IMAGE_IGNORE_ALPHA(color->type) == IMAGE_RGBA16_COLOR) {
                        new_color.ga8.gray = (color->rgba16.red & 0xFF + color->rgba16.green & 0xFF + color->rgba16.blue & 0xFF) / 3;
                        new_color.ga8.alpha = color->rgba16.alpha & 0xFF;
                    }

                    break;
                }
                case IMAGE_GRAY16_COLOR: {
                    if (IMAGE_IGNORE_ALPHA(color->type) == IMAGE_RGBA8_COLOR) {
                        new_color.ga16.gray = twice16((color->rgba8.red + color->rgba8.green + color->rgba8.blue) / 3);
                        new_color.ga16.alpha = twice16(color->rgba8.alpha);
                    } else if (IMAGE_IGNORE_ALPHA(color->type) == IMAGE_RGBA16_COLOR) {
                        new_color.ga16.gray = twice16((color->rgba16.red & 0xFF + color->rgba16.green & 0xFF + color->rgba16.blue & 0xFF) / 3);
                        new_color.ga16.alpha = color->rgba16.alpha;
                    }

                    break;
                }
            }

            break;
        }
        case IMAGE_GRAY8_COLOR:
        case IMAGE_GRAY16_COLOR: {
            switch (IMAGE_IGNORE_ALPHA(type)) {
                case IMAGE_RGBA8_COLOR: {
                    if (IMAGE_IGNORE_ALPHA(color->type) == IMAGE_GRAY8_COLOR) {
                        new_color.rgba8.red = color->ga8.gray;
                        new_color.rgba8.green = color->ga8.gray;
                        new_color.rgba8.blue = color->ga8.gray;
                        new_color.rgba8.alpha = color->ga8.alpha;
                    } else if (IMAGE_IGNORE_ALPHA(color->type) == IMAGE_GRAY16_COLOR) {
                        new_color.rgba8.red = color->ga16.gray & 0xFF;
                        new_color.rgba8.green = color->ga16.gray & 0xFF;
                        new_color.rgba8.blue = color->ga16.gray & 0xFF;
                        new_color.rgba8.alpha = color->ga16.alpha & 0xFF;
                    }

                    break;
                }
                case IMAGE_RGBA16_COLOR: {
                    if (IMAGE_IGNORE_ALPHA(color->type) == IMAGE_GRAY8_COLOR) {
                        new_color.rgba16.red = twice16(color->ga8.gray);
                        new_color.rgba16.green = twice16(color->ga8.gray);
                        new_color.rgba16.blue = twice16(color->ga8.gray);
                        new_color.rgba16.alpha = twice16(color->ga8.alpha);
                    } else if (IMAGE_IGNORE_ALPHA(color->type) == IMAGE_GRAY16_COLOR) {
                        new_color.rgba16.red = twice16(color->ga16.gray);
                        new_color.rgba16.green = twice16(color->ga16.gray);
                        new_color.rgba16.blue = twice16(color->ga16.gray);
                        new_color.rgba16.alpha = twice16(color->ga16.alpha);
                    }

                    break;
                }
                case IMAGE_GRAY8_COLOR: {
                    if (IMAGE_IGNORE_ALPHA(color->type) == IMAGE_GRAY16_COLOR) {
                        new_color.ga8.gray = color->ga16.gray & 0xFF;
                        new_color.ga8.alpha = color->ga16.alpha & 0xFF;
                    }

                    break;
                }
                case IMAGE_GRAY16_COLOR: {
                    if (IMAGE_IGNORE_ALPHA(color->type) == IMAGE_GRAY8_COLOR) {
                        new_color.ga16.gray = twice16(color->ga8.gray);
                        new_color.ga16.alpha = twice16(color->ga8.alpha);
                    }

                    break;
                }
            }

            break;
        }
    }

    *color = new_color;
}

static void _png_copy_chunk(struct image_png_chunk* src, struct image_png_chunk* dest) {
    dest->length = src->length;
    strcpy(dest->type, src->type);
    dest->data = malloc(sizeof(uint8_t) * src->length);
    memcpy(dest->data, src->data, sizeof(uint8_t) * src->length);
    dest->crc = src->crc;
    dest->location = src->location;
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

    idat->type = PNG_IDAT_SCANLINES;
    idat->size = 0;
    idat->data = malloc(0);
    idat->location = chunk->location;

    z_stream stream;
    memset(&stream, 0, sizeof(z_stream));

    inflateInit(&stream);

    stream.avail_in = chunk->length;
    stream.next_in = chunk->data;

    uint32_t times = 1;
    do {
        idat->size = 1024 * times;
        idat->data = realloc(idat->data, sizeof(uint8_t) * idat->size);

        stream.avail_out = 1024;
        stream.next_out = idat->data + 1024 * (times - 1);

        times++;

        inflate(&stream, Z_FINISH);
    } while (stream.avail_out == 0);

    // re-adjust
    idat->size -= stream.avail_out;
    idat->data = realloc(idat->data, sizeof(uint8_t) * idat->size);

    inflateEnd(&stream);

    return 0;
}

static void _png_write_chunk_IHDR(struct image_png_chunk_IHDR* ihdr, struct image_png_chunk* chunk) {
    chunk->length = 13;
    strcpy(chunk->type, "IHDR");

    if (chunk->data == NULL) {
        chunk->data = malloc(sizeof(uint8_t) * 13);
    } else {
        chunk->data = realloc(chunk->data, sizeof(uint8_t) * 13);
    }

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

    if (chunk->data == NULL) {
        chunk->data = malloc(0);
    }

    z_stream stream;
    memset(&stream, 0, sizeof(z_stream));

    deflateInit(&stream, 9);

    stream.avail_in = idat->size;
    stream.next_in = idat->data;

    // fetch until available output size be 0
    uint32_t times = 1;
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

static void _png_IDAT_to_scanlines(struct image_png_chunk_IHDR* ihdr, struct image_png_chunk_IDAT* idat) {
    idat->type = PNG_IDAT_SCANLINES;

    uint32_t size = idat->size;
    uint8_t* pixels = idat->data;

    uint32_t pixel_size = PNG_BITS_TYPE[ihdr->color][ihdr->depth] / 8;

    idat->size = ihdr->width * ihdr->height * pixel_size + ihdr->height;
    idat->data = malloc(sizeof(uint8_t) * idat->size);

    // for now, all scanlines are filtered to 0

    uint64_t index = 0;

    for (uint32_t y = 0; y < ihdr->height; y++) {
        // set filter to scanline
        idat->data[index++] = 0;

        for (uint32_t x = 0; x < ihdr->width; x++) {
            for (uint8_t j = 0; j < pixel_size; j++) {
                idat->data[index++] = pixels[(x + y * ihdr->width) * pixel_size + j];
            }
        }
    }

    free(pixels);
}

static void _png_IDAT_to_pixels(struct image_png_chunk_IHDR* ihdr, struct image_png_chunk_IDAT* idat) {
    idat->type = PNG_IDAT_PIXELS;

    uint32_t size = idat->size;
    uint8_t* scanlines = idat->data;

    uint32_t pixel_size = PNG_BITS_TYPE[ihdr->color][ihdr->depth] / 8;

    idat->size = ihdr->width * ihdr->height * pixel_size;
    idat->data = malloc(sizeof(uint8_t) * idat->size);

    uint64_t index = 0;

    for (uint32_t y = 0; y < ihdr->height; y++) {
        uint8_t filter = scanlines[index++];

        for (uint32_t x = 0; x < ihdr->width; x++) {
            for (uint8_t j = 0; j < pixel_size; j++) {
                uint32_t a = x > 0 ? scanlines[index - pixel_size] : 0;
                uint32_t b = y > 0 ? scanlines[index - y * (ihdr->width + 1)] : 0;
                uint32_t byte = scanlines[index];

                switch (filter) {
                    case 0: break;
                    case 1: byte = (byte + a); break;
                    case 2: byte = (byte + b); break;
                    case 3: byte = (byte + floor(a / 2.0 + b / 2.0)); break;
                }

                byte %= 256;

                // presave byte in scanline for future usage
                scanlines[index++] = byte;
                idat->data[(x + y * ihdr->width) * pixel_size + j] = byte;
            }
        }
    }

    free(scanlines);
}

static void _png_convert_chunk_IDAT(struct image_png_chunk_IHDR* ihdr,
                                    struct image_png_chunk_IDAT* idat,
                                    enum image_png_idat_type type) {
    // nothing to do
    if (idat->type == type) {
        return;
    }

    switch (type) {
        case PNG_IDAT_SCANLINES: _png_IDAT_to_scanlines(ihdr, idat); break;
        case PNG_IDAT_PIXELS: _png_IDAT_to_pixels(ihdr, idat); break;
    }
}

static inline void _png_free_chunk_IDAT(struct image_png_chunk_IDAT* idat) {
    free(idat->data);
}

static void _png_execute_pixel(struct image_png* image, uint32_t x, uint32_t y,
                               _png_pixel_fn action, struct image_color* color) {
    struct image_png_chunk_IHDR* ihdr = &image->ihdr;
    struct image_png_chunk_IDAT* idat = &image->idat;

    uint8_t type = ihdr->color;
    uint8_t depth = ihdr->depth;

    uint32_t pixel_size = PNG_BITS_TYPE[type][depth] / 8;
    uint64_t pixel_index = (x + y * ihdr->width) * pixel_size;

    // out of bounds
    if (idat->size <= pixel_index) {
        return;
    }

    action(ihdr, &idat->data[pixel_index], color);
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
                    color->type |= IMAGE_ALPHA_BIT;
                    color->ga8.alpha = pixel_8bits[1];
                }
            } else if (depth == 16) {
                color->type = IMAGE_GRAY16_COLOR;
                color->ga16.gray = pixel_16bits[0];

                // if alpha is present
                if (ihdr->color == 4) {
                    color->type |= IMAGE_ALPHA_BIT;
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
                    color->type |= IMAGE_ALPHA_BIT;
                    color->rgba8.alpha = pixel_8bits[3];
                }
            } else if (depth == 16) {
                color->type = IMAGE_RGBA16_COLOR;
                color->rgba16.red = pixel_16bits[0];
                color->rgba16.green = pixel_16bits[1];
                color->rgba16.blue = pixel_16bits[2];

                // if alpha is present
                if (ihdr->color == 6) {
                    color->type |= IMAGE_ALPHA_BIT;
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
