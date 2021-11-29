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

struct image_png_chunk_PLTE {
    uint16_t size;
    struct image_color* pallete;
};

enum image_png_trns_type {
    PNG_tRNS_8BITS,
    PNG_tRNS_16BITS
};

struct image_png_chunk_tRNS {
    enum image_png_trns_type type;
    uint32_t size;
    union {
        uint8_t* data_8bits;
        uint16_t* data_16bits;
    };
};

struct image_png_chunk_cHRM {
    uint32_t white_px;
    uint32_t white_py;
    uint32_t red_x;
    uint32_t red_y;
    uint32_t green_x;
    uint32_t green_y;
    uint32_t blue_x;
    uint32_t blue_y;
};

struct image_png_chunk_gAMA {
    uint32_t gamma;
};

struct image_png_chunk_iCCP {
    char name[80];
    uint8_t compression;
    uint32_t size;
    uint8_t* data;
};

enum image_png_sbit_type {
    PNG_sBIT_GREY,
    PNG_sBIT_RGB_OR_INDEXED,
    PNG_sBIT_GREYALPHA,
    PNG_sBIT_RGBALPHA
};

struct image_png_chunk_sBIT {
    enum image_png_sbit_type type;

    uint8_t grey;
    uint8_t red;
    uint8_t green;
    uint8_t blue; 
    uint8_t alpha;
};

struct image_png_chunk_sRGB {
    uint8_t rendering;
};

struct image_png_chunk_tEXt {
    char keyword[80];
    char* text;
};

struct image_png_chunk_zTXt {
    char keyword[80];
    uint8_t compression;
    char* text;
};

struct image_png_chunk_iTXt {
    char keyword[80];
    uint8_t compression_flag;
    uint8_t compression_method;
    char* language_tag;
    char* translated_keyword;
    char* text;
};

enum png_textual_data_type {
    PNG_TEXTUAL_UNCOMPRESSED,
    PNG_TEXTUAL_COMPRESSED,
    PNG_TEXTUAL_INTERNATIONAL
};

struct png_textual_data {
    enum png_textual_data_type type;
    union {
        struct image_png_chunk_tEXt text;
        struct image_png_chunk_zTXt ztxt;
        struct image_png_chunk_iTXt itxt;
    } data;
};

struct png_textual_list {
    uint32_t size;
    struct png_textual_data* list;
};

struct image_png_chunk_tIME {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
};

enum image_png_idat_type {
    PNG_IDAT_SCANLINES,
    PNG_IDAT_PIXELS
};

struct image_png_chunk_IDAT {
    enum image_png_idat_type type;
    size_t size;
    uint8_t* data;
};

struct image_png {
    struct image_png_chunk_IHDR ihdr;
    struct image_png_chunk_PLTE plte;

    // there is no trns if size is 0
    struct image_png_chunk_tRNS trns;
    // there is no chrm if all is set up to 0
    struct image_png_chunk_cHRM chrm;
    // there is no gama if gamma value is 0
    struct image_png_chunk_gAMA gama;
    // there is no iccp if all is set up to 0
    struct image_png_chunk_iCCP iccp;
    // there is no sbit if all is set up to 0 according to type
    struct image_png_chunk_sBIT sbit;
    // there is no srgb if rendering is out of range between 0 and 3
    struct image_png_chunk_sRGB srgb;
    // there is no text if size is 0
    struct png_textual_list textual_list;
    // there is no time if all is set up to 0
    struct image_png_chunk_tIME time;

    // IDAT needs to be in PIXELS instead of SCANLINES
    struct image_png_chunk_IDAT idat;
};

static inline uint32_t convert_int_be(uint32_t value);
static void _png_convert_color(struct image_color* color, enum image_color_type type);

static void _png_copy_chunk(struct image_png_chunk* src, struct image_png_chunk* dest);

static inline void _png_generate_crc32(struct image_png_chunk* chunk);
// return 0 if it's alright otherwise another number if corrupted
static inline int _png_check_crc32(struct image_png_chunk* chunk);

static inline void _png_populate_chunk(struct image_png_chunk* chunk, size_t bytes);

// return 0 if success, otherwise another number
// ihdr can be null, just checking if chunk is an IHDR valid
static int _png_read_chunk_IHDR(struct image_png_chunk* chunk, struct image_png_chunk_IHDR* ihdr);
// return 0 if sucess, otherwise another number
static int _png_read_chunk_PLTE(struct image_png_chunk* chunk, struct image_png_chunk_PLTE* plte);
// return 0 if success, also type is gonna be 8BITS by default, check _png_convert_chunk_tRNS
static int _png_read_chunk_tRNS(struct image_png_chunk* chunk, struct image_png_chunk_tRNS* trns);
// return 0 if success, otherise another number
static int _png_read_chunk_cHRM(struct image_png_chunk* chunk, struct image_png_chunk_cHRM* chrm);
// return 0 if success, otherise another number
static int _png_read_chunk_gAMA(struct image_png_chunk* chunk, struct image_png_chunk_gAMA* gama);
static int _png_read_chunk_iCCP(struct image_png_chunk* chunk, struct image_png_chunk_iCCP* iccp);
static int _png_read_chunk_sBIT(struct image_png_chunk* chunk, struct image_png_chunk_sBIT* sbit);
static int _png_read_chunk_sRGB(struct image_png_chunk* chunk, struct image_png_chunk_sRGB* srgb);
static int _png_read_chunk_tEXt(struct image_png_chunk* chunk, struct image_png_chunk_tEXt* text);
static int _png_read_chunk_zTXt(struct image_png_chunk* chunk, struct image_png_chunk_zTXt* ztxt);
static int _png_read_chunk_iTXt(struct image_png_chunk* chunk, struct image_png_chunk_iTXt* itxt);
// return 0 if success, otherwise another number
static int _png_read_chunk_tIME(struct image_png_chunk* chunk, struct image_png_chunk_tIME* time);
// this is only based in zlib and it'll write in IDAT chunk as SCANLINES
static int _png_read_chunk_IDAT(struct image_png_chunk* chunk, struct image_png_chunk_IDAT* idat);

static void _png_write_chunk_IHDR(struct image_png_chunk_IHDR* ihdr, struct image_png_chunk* chunk);
static void _png_write_chunk_PLTE(struct image_png_chunk_PLTE* plte, struct image_png_chunk* chunk);
// it needs to be in 8BITS type
static void _png_write_chunk_tRNS(struct image_png_chunk_tRNS* trns, struct image_png_chunk* chunk);
static void _png_write_chunk_cHRM(struct image_png_chunk_cHRM* chrm, struct image_png_chunk* chunk);
static void _png_write_chunk_gAMA(struct image_png_chunk_gAMA* gama, struct image_png_chunk* chunk);
static void _png_write_chunk_iCCP(struct image_png_chunk_iCCP* iccp, struct image_png_chunk* chunk);
static void _png_write_chunk_sBIT(struct image_png_chunk_sBIT* sbit, struct image_png_chunk* chunk);
static void _png_write_chunk_sRGB(struct image_png_chunk_sRGB* srgb, struct image_png_chunk* chunk);
static void _png_write_chunk_tEXt(struct image_png_chunk_tEXt* text, struct image_png_chunk* chunk);
static void _png_write_chunk_zTXt(struct image_png_chunk_zTXt* ztxt, struct image_png_chunk* chunk);
static void _png_write_chunk_iTXt(struct image_png_chunk_iTXt* itxt, struct image_png_chunk* chunk);
static void _png_write_chunk_tIME(struct image_png_chunk_tIME* time, struct image_png_chunk* chunk);
// this is only based in zlib, also it needs that IDAT chunk be in SCANLINES
static void _png_write_chunk_IDAT(struct image_png_chunk_IDAT* idat, struct image_png_chunk* chunk);

static void _png_convert_chunk_tRNS(struct image_png_chunk_tRNS* trns,
                                    enum image_png_trns_type type);
static void _png_convert_chunk_IDAT(struct image_png_chunk_IHDR* ihdr,
                                    struct image_png_chunk_IDAT* idat,
                                    enum image_png_idat_type type);

static inline enum image_png_sbit_type _png_color_to_sbit(uint8_t color);

static inline int _png_check_chrm(struct image_png_chunk_cHRM* chrm);
static inline int _png_check_iccp(struct image_png_chunk_iCCP* iccp);
static inline int _png_check_sbit(struct image_png_chunk_sBIT* sbit);
// return 0 if no time, otherwise any number if there is time
static inline int _png_check_time(struct image_png_chunk_tIME* time);

static void _png_add_text(struct png_textual_list* list,
                          struct png_textual_data* textual);

static void _png_get_text(struct png_textual_list* list,
                          const char* keyword,
                          struct png_textual_data** out_textual,
                          uint32_t* index);

static void _png_sub_text(struct png_textual_list* list,
                          const char* keyword);

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

    image->plte.size = 0;
    image->plte.pallete = malloc(0);

    image->trns.type = PNG_tRNS_8BITS;
    image->trns.size = 0;
    image->trns.data_8bits = NULL;

    memset(&image->chrm, 0, sizeof(struct image_png_chunk_cHRM));
    memset(&image->gama, 0, sizeof(struct image_png_chunk_gAMA));
    memset(&image->iccp, 0, sizeof(struct image_png_chunk_iCCP));
    memset(&image->sbit, 0, sizeof(struct image_png_chunk_sBIT));
    memset(&image->srgb, -1, sizeof(struct image_png_chunk_sRGB));
    memset(&image->textual_list, 0, sizeof(struct png_textual_list));
    memset(&image->time, 0, sizeof(struct image_png_chunk_tIME));

    image->sbit.type = _png_color_to_sbit(ihdr->color);

    struct image_png_chunk_IDAT* idat = &image->idat;
    idat->type = PNG_IDAT_PIXELS;
    uint32_t pixel_size = PNG_BITS_TYPE[ihdr->color][ihdr->depth] / 8;
    idat->size = width * height * pixel_size;
    idat->data = malloc(sizeof(uint8_t) * idat->size);
    memset(idat->data, 0, sizeof(uint8_t) * idat->size);

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
    image->plte.size = 0;
    image->plte.pallete = malloc(0);
    image->trns.type = PNG_tRNS_8BITS;
    image->trns.size = 0;
    image->trns.data_8bits = NULL;
    image->idat.data = NULL;
    memset(&image->chrm, 0, sizeof(struct image_png_chunk_cHRM));
    memset(&image->gama, 0, sizeof(struct image_png_chunk_gAMA));
    memset(&image->iccp, 0, sizeof(struct image_png_chunk_iCCP));
    memset(&image->sbit, 0, sizeof(struct image_png_chunk_sBIT));
    memset(&image->srgb, -1, sizeof(struct image_png_chunk_sRGB));
    memset(&image->textual_list, 0, sizeof(struct png_textual_list));
    memset(&image->time, 0, sizeof(struct image_png_chunk_tIME));

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

        printf("Chunk: %d %s\n", chunk.length, chunk.type);

        int invalid = 0;

        if (strcmp(chunk.type, "IHDR") == 0) {
            int ihdr_ret = _png_read_chunk_IHDR(&chunk, &image->ihdr);

            if (location != 0 || ihdr_ret != 0 || image->ihdr.compression != 0) {
                // IHDR is invalid, it's not the first chunk or compression is not zlib
                invalid = 1;
            }
        } else if (strcmp(chunk.type, "PLTE") == 0) {
            int plte_ret = _png_read_chunk_PLTE(&chunk, &image->plte);

            if (location == 0 || idat_chunk.length > 0 || plte_ret != 0) {
                // PLTE is before than IHDR, IDAT was read before than PLTE or PLTE is invalid
                invalid = 1;
            }
        } else if (strcmp(chunk.type, "tRNS") == 0) {
            int trns_ret = _png_read_chunk_tRNS(&chunk, &image->trns);
            uint8_t color = image->ihdr.color;
            
            if (location == 0 || idat_chunk.length > 0 || trns_ret != 0) {
                invalid = 1;
            } else if (color == 0 || color == 2) {
                _png_convert_chunk_tRNS(&image->trns, PNG_tRNS_16BITS);
            }
        } else if (strcmp(chunk.type, "cHRM") == 0) {
            int chrm_ret = _png_read_chunk_cHRM(&chunk, &image->chrm);

            if (location == 0 || chrm_ret != 0) {
                invalid = 1;
            }
        } else if (strcmp(chunk.type, "gAMA") == 0) {
            int gama_ret = _png_read_chunk_gAMA(&chunk, &image->gama);

            if (location == 0 || gama_ret != 0) {
                invalid = 1;
            }
        } else if (strcmp(chunk.type, "iCCP") == 0) {
            int iccp_ret = _png_read_chunk_iCCP(&chunk, &image->iccp);

            if (location == 0 || iccp_ret != 0) {
                invalid = 1;
            }
        } else if (strcmp(chunk.type, "sBIT") == 0) {
            int sbit_ret = _png_read_chunk_sBIT(&chunk, &image->sbit);

            if (location == 0 || sbit_ret != 0) {
                invalid = 1;
            }
        } else if (strcmp(chunk.type, "sRGB") == 0) {
            int srgb_ret = _png_read_chunk_sRGB(&chunk, &image->srgb);

            if (location == 0 || srgb_ret != 0) {
                invalid = 1;
            }
        } else if (strcmp(chunk.type, "tEXt") == 0) {
            struct image_png_chunk_tEXt text;
            int text_ret = _png_read_chunk_tEXt(&chunk, &text);

            if (location == 0 || text_ret != 0) {
                invalid = 1;
            } else {
                struct png_textual_data data;
                data.type = PNG_TEXTUAL_UNCOMPRESSED;
                memcpy(&data.data.text, &text, sizeof(struct image_png_chunk_tEXt));

                _png_add_text(&image->textual_list, &data);
            }
        } else if (strcmp(chunk.type, "zTXt") == 0) {
            struct image_png_chunk_zTXt ztxt;
            int ztxt_ret = _png_read_chunk_zTXt(&chunk, &ztxt);

            if (location == 0 || ztxt_ret != 0) {
                invalid = 1;
            } else {
                struct png_textual_data data;
                data.type = PNG_TEXTUAL_COMPRESSED;
                memcpy(&data.data.ztxt, &ztxt, sizeof(struct image_png_chunk_zTXt));

                _png_add_text(&image->textual_list, &data);
            }
        } else if (strcmp(chunk.type, "iTXt") == 0) {
            struct image_png_chunk_iTXt itxt;
            int itxt_ret = _png_read_chunk_iTXt(&chunk, &itxt);

            if (location == 0 || itxt_ret != 0) {
                invalid = 1;
            } else {
                struct png_textual_data data;
                data.type = PNG_TEXTUAL_INTERNATIONAL;
                memcpy(&data.data.itxt, &itxt, sizeof(struct image_png_chunk_iTXt));

                _png_add_text(&image->textual_list, &data);
            }
        } else if (strcmp(chunk.type, "tIME") == 0) {
            int time_ret = _png_read_chunk_tIME(&chunk, &image->time);

            if (location == 0 || time_ret != 0) {
                invalid = 1;
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

            if (location == 0) {
                // IDAT is before than IHDR
                invalid = 1;
            }
        }

        free(chunk.data);

        if (invalid != 0) {
            image_png_close(image);
            image = NULL;
            break;
        }

        location++;
    } while (strcmp(chunk.type, "IEND") != 0);

    if (image != NULL) {
        image->sbit.type = _png_color_to_sbit(image->ihdr.color);

        _png_read_chunk_IDAT(&idat_chunk, &image->idat);
        _png_convert_chunk_IDAT(&image->ihdr, &image->idat, PNG_IDAT_PIXELS);

        free(idat_chunk.data);
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

    image->sbit.type = _png_color_to_sbit(image->ihdr.color);

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

void image_png_get_gamma(struct image_png* image, uint32_t* gamma) {
    *gamma = image->gama.gamma;
}

void image_png_set_gamma(struct image_png* image, uint32_t gamma) {
    image->gama.gamma = gamma;
}

void image_png_get_sbit(struct image_png* image, struct image_color* color) {
    struct image_png_chunk_sBIT* sbit = &image->sbit;

    switch (sbit->type) {
        case PNG_sBIT_GREY:
        case PNG_sBIT_GREYALPHA: {
            color->type = IMAGE_GRAY8_COLOR;
            color->ga8.gray = sbit->grey;

            if (sbit->type == PNG_sBIT_GREYALPHA) {
                color->type |= IMAGE_ALPHA_BIT;
                color->ga8.alpha = sbit->alpha;
            }

            break;
        }
        case PNG_sBIT_RGB_OR_INDEXED:
        case PNG_sBIT_RGBALPHA: {
            color->type = IMAGE_RGBA8_COLOR;
            color->rgba8.red = sbit->red;
            color->rgba8.green = sbit->green;
            color->rgba8.blue = sbit->blue;

            if (sbit->type == PNG_sBIT_RGBALPHA) {
                color->type |= IMAGE_ALPHA_BIT;
                color->rgba8.alpha = sbit->alpha;
            }

            break;
        }
    }
}

void image_png_set_sbit(struct image_png* image, struct image_color color) {
    struct image_png_chunk_sBIT* sbit = &image->sbit;

    switch (sbit->type) {
        case PNG_sBIT_GREY:
        case PNG_sBIT_GREYALPHA: {
            sbit->grey = color.ga8.gray;

            if (sbit->type == PNG_sBIT_GREYALPHA) {
                sbit->alpha = color.ga8.alpha;
            }

            break;
        }
        case PNG_sBIT_RGB_OR_INDEXED:
        case PNG_sBIT_RGBALPHA: {
            sbit->red = color.rgba8.red;
            sbit->green = color.rgba8.green;
            sbit->blue = color.rgba8.blue;

            if (sbit->type == PNG_sBIT_RGBALPHA) {
                sbit->alpha = color.rgba8.alpha;
            }

            break;
        }
    }
}

void image_png_get_srgb(struct image_png* image, uint8_t* rendering) {
    *rendering = image->srgb.rendering;
}

void image_png_set_srgb(struct image_png* image, uint8_t rendering) {
    image->srgb.rendering = rendering; 
}

void image_png_set_text(struct image_png* image, const char* keyword, const char* text, int16_t compress) {
    struct png_textual_data* textual;
    _png_get_text(&image->textual_list, keyword, &textual, NULL);

    if (textual != NULL) {
        if (textual->type == PNG_TEXTUAL_INTERNATIONAL) {
            textual->data.itxt.compression_method = compress & 0xFF;
        } else {
            if (compress == -1) {
                // don't compress textual info            
                if (textual->type == PNG_TEXTUAL_COMPRESSED) {
                    struct image_png_chunk_tEXt text;
                    memcpy(text.keyword, textual->data.ztxt.keyword, 80);
                    text.text = textual->data.ztxt.text;

                    textual->type = PNG_TEXTUAL_UNCOMPRESSED;
                    memcpy(&textual->data.text, &text, sizeof(struct image_png_chunk_tEXt));
                }
            } else if (compress >= 0) {
                // compress textual info
                if (textual->type == PNG_TEXTUAL_UNCOMPRESSED) {
                    struct image_png_chunk_zTXt ztxt;
                    memcpy(ztxt.keyword, textual->data.text.keyword, 80);
                    ztxt.text = textual->data.text.text;

                    textual->type = PNG_TEXTUAL_COMPRESSED;
                    memcpy(&textual->data.ztxt, &ztxt, sizeof(struct image_png_chunk_zTXt));
                }

                textual->data.ztxt.compression = compress;
            }
        }

        if (text != NULL) {
            size_t size = strlen(text) + 1;
            char* ptext;

            if (textual->type == PNG_TEXTUAL_UNCOMPRESSED) {
                ptext = textual->data.text.text = realloc(textual->data.text.text, sizeof(char) * size);
            } else if (textual->type == PNG_TEXTUAL_COMPRESSED) {
                ptext = textual->data.ztxt.text = realloc(textual->data.ztxt.text, sizeof(char) * size);
            } else if (textual->type == PNG_TEXTUAL_INTERNATIONAL) {
                ptext = textual->data.itxt.text = realloc(textual->data.itxt.text, sizeof(char) * size);
            }

            memcpy(ptext, text, size);
        }
    } else {
        if (text == NULL) {
            text = "";
        }

        struct image_png_chunk_zTXt ztxt_stack;
        struct image_png_chunk_tEXt text_stack;
        struct png_textual_data data;

        char* pkeyword;

        size_t size = strlen(text) + 1;
        char* ptext = malloc(sizeof(char) * size);
        memcpy(ptext, text, size);

        if (compress >= 0) {
            pkeyword = ztxt_stack.keyword;
            ztxt_stack.compression = compress;
            ztxt_stack.text = ptext;

            data.type = PNG_TEXTUAL_COMPRESSED;
            memcpy(&data.data.ztxt, &ztxt_stack, sizeof(struct image_png_chunk_zTXt));
        } else {
            pkeyword = text_stack.keyword;
            text_stack.text = ptext;

            data.type = PNG_TEXTUAL_UNCOMPRESSED;
            memcpy(&data.data.text, &text_stack, sizeof(struct image_png_chunk_tEXt));
        }

        strncpy(text_stack.keyword, keyword, 80);
        // make sure about null terminator
        text_stack.keyword[strlen(text_stack.keyword)] = '\0';
 
        _png_add_text(&image->textual_list, &data);
    }
}

void image_png_set_itxt(struct image_png* image, const char* keyword, int16_t compression_flag, int16_t compression_method,
                        const char* language_tag, const char* translated_keyword, const char* text) {
    struct png_textual_data* textual;
    _png_get_text(&image->textual_list, keyword, &textual, NULL);

    if (textual != NULL) {
        if (textual->type == PNG_TEXTUAL_UNCOMPRESSED) {
            struct image_png_chunk_iTXt itxt;
            strncpy(itxt.keyword, textual->data.text.keyword, 80);

            itxt.compression_flag = 0;
            itxt.compression_method = 0;

            itxt.language_tag = strdup("");
            itxt.translated_keyword = strdup("");
            itxt.text = strdup(textual->data.itxt.text);

            textual->type = PNG_TEXTUAL_INTERNATIONAL;
            memcpy(&textual->data.itxt, &itxt, sizeof(struct image_png_chunk_iTXt));
        } else if (textual->type == PNG_TEXTUAL_COMPRESSED) {
            struct image_png_chunk_iTXt itxt;
            strncpy(itxt.keyword, textual->data.ztxt.keyword, 80);

            itxt.compression_flag = 0;
            itxt.compression_method = textual->data.ztxt.compression + 1;

            itxt.language_tag = strdup("");
            itxt.translated_keyword = strdup("");
            itxt.text = strdup(textual->data.ztxt.text);

            textual->type = PNG_TEXTUAL_INTERNATIONAL;
            memcpy(&textual->data.itxt, &itxt, sizeof(struct image_png_chunk_iTXt));
        }

        if (compression_flag != -1) {
            textual->data.itxt.compression_flag = compression_flag & 0xFF;
        }

        if (compression_method != -1) {
            textual->data.itxt.compression_method = compression_method & 0xFF;
        }

        if (language_tag != NULL) {
            free(textual->data.itxt.language_tag);
            textual->data.itxt.language_tag = strdup(language_tag);
        }

        if (translated_keyword != NULL) {
            free(textual->data.itxt.translated_keyword);
            textual->data.itxt.translated_keyword = strdup(translated_keyword);
        }

        if (text != NULL) {
            free(textual->data.itxt.text);
            textual->data.itxt.text = strdup(text);
        }
    } else {
        if (language_tag == NULL) {
            language_tag = "";
        }
        if (translated_keyword == NULL) {
            translated_keyword = "";
        }
        if (text == NULL) {
            text = "";
        }

        struct png_textual_data data;
        data.type = PNG_TEXTUAL_INTERNATIONAL;

        struct image_png_chunk_iTXt* itxt = &data.data.itxt;
        strncpy(itxt->keyword, keyword, 80);
        itxt->keyword[strlen(itxt->keyword)] = '\0';

        itxt->compression_flag = compression_flag;
        itxt->compression_method = compression_method;

        itxt->language_tag = strdup(language_tag);
        itxt->translated_keyword = strdup(translated_keyword);
        itxt->text = strdup(text);

        _png_add_text(&image->textual_list, &data);
    }
}

void image_png_get_text(struct image_png* image, const char* keyword, char** out_text, int16_t* out_compress) {
    struct png_textual_data* textual;
    _png_get_text(&image->textual_list, keyword, &textual, NULL);

    if (textual != NULL) {
        char* text;
        int16_t compress;
        
        if (textual->type == PNG_TEXTUAL_UNCOMPRESSED) {
            text = textual->data.text.text;
            compress = -1;
        } else if (textual->type == PNG_TEXTUAL_COMPRESSED) {
            text = textual->data.ztxt.text;
            compress = textual->data.ztxt.compression;
        } else if (textual->type == PNG_TEXTUAL_INTERNATIONAL) {
            text = textual->data.itxt.text;
            compress = textual->data.itxt.compression_method;
        }

        size_t size = strlen(text) + 1;
        char* copy = malloc(sizeof(char) * size);
        memcpy(copy, text, size);

        *out_text = copy;

        if (out_compress != NULL) {
            *out_compress = compress;
        }
    } else {
        *out_text = NULL;
        if (out_compress != NULL) {
            *out_compress = -2;
        }
    }
}

void image_png_get_itxt(struct image_png* image, const char* keyword, int16_t* out_compression_flag, int16_t* out_compression_method,
                        char** out_language_tag, char** out_translated_keyword, char** out_text) {
    struct png_textual_data* textual;
    _png_get_text(&image->textual_list, keyword, &textual, NULL);

    if (out_compression_flag != NULL) {
        *out_compression_flag = -1;
    }

    if (out_compression_method != NULL) {
        *out_compression_method = -1;
    }

    if (out_language_tag != NULL) {
        *out_language_tag = NULL;
    }

    if (out_translated_keyword != NULL) {
        *out_translated_keyword = NULL;
    }

    if (out_text != NULL) {
        *out_text = NULL;
    }

    if (textual == NULL) {
        return;
    }

    if (textual->type == PNG_TEXTUAL_UNCOMPRESSED) {
        if (out_text != NULL) {
            *out_text = strdup(textual->data.text.text);
        }
    } else if (textual->type == PNG_TEXTUAL_COMPRESSED) {
        if (out_compression_method != NULL) {
            *out_compression_method = textual->data.ztxt.compression + 1;
        }

        if (out_text != NULL) {
            *out_text = strdup(textual->data.ztxt.text);
        }
    } else if (textual->type == PNG_TEXTUAL_INTERNATIONAL) {
        if (out_compression_flag != NULL) {
            *out_compression_flag = textual->data.itxt.compression_flag;
        }

        if (out_compression_method != NULL) {
            *out_compression_method = textual->data.itxt.compression_method;
        }

        if (out_language_tag != NULL) {
            *out_language_tag = strdup(textual->data.itxt.language_tag);
        }

        if (out_translated_keyword != NULL) {
            *out_translated_keyword = strdup(textual->data.itxt.translated_keyword);
        }

        if (out_text != NULL) {
            *out_text = strdup(textual->data.itxt.text);
        }
    }
}

void image_png_get_keys(struct image_png* image, char*** out_keywords, uint32_t* size) {
    char** keywords = malloc(sizeof(char*) * image->textual_list.size);

    for (uint32_t i = 0; i < image->textual_list.size; i++) {
        struct png_textual_data* textual = &image->textual_list.list[i];
        const char* keyword;

        if (textual->type == PNG_TEXTUAL_UNCOMPRESSED) {
            keyword = textual->data.text.keyword;
        } else if (textual->type == PNG_TEXTUAL_COMPRESSED) {
            keyword = textual->data.ztxt.keyword;
        } else if (textual->type == PNG_TEXTUAL_INTERNATIONAL) {
            keyword = textual->data.itxt.keyword;
        }

        size_t keyword_length = strlen(keyword) + 1;

        char* copy_keyword = malloc(sizeof(char) * keyword_length);
        memcpy(copy_keyword, keyword, keyword_length);

        keywords[i] = copy_keyword;
    }

    *out_keywords = keywords;
    *size = image->textual_list.size;
}

void image_png_del_text(struct image_png* image, const char* keyword) {
    _png_sub_text(&image->textual_list, keyword);
}

void image_png_get_palette(struct image_png* image, uint16_t* psize, struct image_color** ppalette) {
    uint16_t size = image->plte.size;
    struct image_color* pallete = malloc(sizeof(struct image_color) * 3 * size);
    memcpy(pallete, image->plte.pallete, sizeof(struct image_color) * 3 * size);

    *psize = size;
    *ppalette = pallete;
}

void image_png_set_palette(struct image_png* image, uint16_t size, struct image_color* pallete) {
    if (size > 256) {
        size = 256;
    }

    struct image_png_chunk_PLTE* plte = &image->plte;

    if (size > 0) {
        plte->size = size;
        plte->pallete = realloc(plte->pallete, sizeof(struct image_color) * 3 * size);
        memcpy(&plte->pallete, pallete, sizeof(struct image_color) * 3 * size);
    } else if (image->ihdr.color == 3) {
        // if indexed, it needs at least to have 1 pallete

        plte->size = 1;
        plte->pallete = realloc(plte->pallete, sizeof(struct image_color));
        memset(plte->pallete, 0, sizeof(struct image_color));
    }
}

void image_png_get_pixel(struct image_png* image, uint32_t x, uint32_t y, struct image_color* color) {
    _png_execute_pixel(image, x, y, _png_get_pixel, color);
}

void image_png_set_pixel(struct image_png* image, uint32_t x, uint32_t y, struct image_color color) {
    _png_execute_pixel(image, x, y, _png_set_pixel, &color);
}

void image_png_get_timestamp(struct image_png* image, struct image_time* time) {
    time->year = image->time.year;
    time->month = image->time.month;
    time->day = image->time.day;
    time->hour = image->time.hour;
    time->minute = image->time.minute;
    time->second = image->time.second;
}

void image_png_set_timestamp(struct image_png* image, struct image_time time) {
    image->time.year = time.year;
    image->time.month = time.month;
    image->time.day = time.day;
    image->time.hour = time.hour;
    image->time.minute = time.minute;
    image->time.second = time.second;
}

struct image_png* image_png_copy(struct image_png* image) {
    struct image_png* copy_image = malloc(sizeof(struct image_png));

    if (copy_image != NULL) {
        memcpy(&copy_image->ihdr, &image->ihdr, sizeof(struct image_png_chunk_IHDR));

        copy_image->plte.size = image->plte.size;
        copy_image->plte.pallete = malloc(sizeof(struct image_color) * copy_image->plte.size);
        memcpy(copy_image->plte.pallete, image->plte.pallete, sizeof(struct image_color) * copy_image->plte.size);

        copy_image->trns.type = image->trns.type;
        copy_image->trns.size = image->trns.size;
        switch (copy_image->trns.type) {
            case PNG_tRNS_8BITS: {
                copy_image->trns.data_8bits = malloc(sizeof(uint8_t) * copy_image->trns.size);
                memcpy(copy_image->trns.data_8bits, image->trns.data_8bits, sizeof(uint8_t) * copy_image->trns.size);
                break;
            }
            case PNG_tRNS_16BITS: {
                copy_image->trns.data_16bits = malloc(sizeof(uint16_t) * copy_image->trns.size);
                memcpy(copy_image->trns.data_16bits, image->trns.data_16bits, sizeof(uint16_t) * copy_image->trns.size);
                break;
            }
        }

        memcpy(&copy_image->chrm, &image->chrm, sizeof(struct image_png_chunk_cHRM));
        memcpy(&copy_image->gama, &image->gama, sizeof(struct image_png_chunk_gAMA));

        strncpy(copy_image->iccp.name, image->iccp.name, 80);
        copy_image->iccp.compression = image->iccp.compression;
        copy_image->iccp.size = image->iccp.size;
        copy_image->iccp.data = malloc(sizeof(uint8_t) * copy_image->iccp.size);
        memcpy(copy_image->iccp.data, image->iccp.data, sizeof(uint8_t) * copy_image->iccp.size);

        memcpy(&copy_image->sbit, &image->sbit, sizeof(struct image_png_chunk_sBIT));
        memcpy(&copy_image->srgb, &image->srgb, sizeof(struct image_png_chunk_sRGB));

        copy_image->textual_list.size = image->textual_list.size;
        copy_image->textual_list.list = malloc(sizeof(struct image_png_chunk_tEXt) * copy_image->textual_list.size);
        for (uint32_t i = 0; i < copy_image->textual_list.size; i++) {
            struct png_textual_data* textual = &image->textual_list.list[i];
            struct png_textual_data* copy_textual = &copy_image->textual_list.list[i];

            copy_textual->type = textual->type;

            if (textual->type == PNG_TEXTUAL_UNCOMPRESSED) {
                struct image_png_chunk_tEXt* text = &textual->data.text;
                struct image_png_chunk_tEXt* copy_text = &copy_textual->data.text;

                strncpy(copy_text->keyword, text->keyword, 80);
                size_t size = strlen(copy_text->text) + 1;
                copy_text->text = malloc(sizeof(char) * size);
                memcpy(copy_text->text, text->text, size);
            } else if (textual->type == PNG_TEXTUAL_COMPRESSED) {
                struct image_png_chunk_zTXt* ztxt = &textual->data.ztxt;
                struct image_png_chunk_zTXt* copy_ztxt = &copy_textual->data.ztxt;

                strncpy(copy_ztxt->keyword, ztxt->keyword, 80);
                copy_ztxt->compression = ztxt->compression; 
                size_t size = strlen(copy_ztxt->text) + 1;
                copy_ztxt->text = malloc(sizeof(char) * size);
                memcpy(copy_ztxt->text, ztxt->text, size);
            } else if (textual->type == PNG_TEXTUAL_INTERNATIONAL) {
                struct image_png_chunk_iTXt* itxt = &textual->data.itxt;
                struct image_png_chunk_iTXt* copy_itxt = &copy_textual->data.itxt;

                strncpy(copy_itxt->keyword, itxt->keyword, 80);
                copy_itxt->compression_flag = itxt->compression_flag;
                copy_itxt->compression_method = itxt->compression_method;
                copy_itxt->language_tag = strdup(itxt->language_tag);
                copy_itxt->translated_keyword = strdup(itxt->translated_keyword);
                copy_itxt->text = strdup(itxt->text);
            }
        }

        memcpy(&copy_image->time, &image->time, sizeof(struct image_png_chunk_tIME));

        copy_image->idat.type = image->idat.type;
        copy_image->idat.size = image->idat.size;
        copy_image->idat.data = malloc(sizeof(uint8_t) * copy_image->idat.size);
        memcpy(copy_image->idat.data, image->idat.data, sizeof(uint8_t) * copy_image->idat.size);
    }

    return copy_image;
}

static inline void _png_addcapicity_tobytes(uint32_t* chunk_size, struct image_png_chunk** pchunks) {
    *chunk_size += 1;
    *pchunks = realloc(*pchunks, *chunk_size * sizeof(struct image_png_chunk));
    memset(&(*pchunks)[*chunk_size - 1], 0, sizeof(struct image_png_chunk)); 
}

void image_png_tobytes(struct image_png* image, uint8_t** pbytes, uint32_t* psize) {
    uint32_t size = 8;
    uint8_t* bytes = malloc(sizeof(uint8_t) * 8);

    memcpy(bytes, PNG_FILE_HEADER, sizeof(uint8_t) * 8);

    uint32_t next_chunk = 0;

    uint32_t chunk_size = 3;
    struct image_png_chunk* chunks = malloc(chunk_size * sizeof(struct image_png_chunk));
    memset(chunks, 0, sizeof(struct image_png_chunk) * chunk_size);

    _png_write_chunk_IHDR(&image->ihdr, &chunks[next_chunk++]);

    if (_png_check_chrm(&image->chrm)) {
        _png_addcapicity_tobytes(&chunk_size, &chunks);
        _png_write_chunk_cHRM(&image->chrm, &chunks[next_chunk++]);
    }
 
    if (image->gama.gamma != 0) {
        _png_addcapicity_tobytes(&chunk_size, &chunks);
        _png_write_chunk_gAMA(&image->gama, &chunks[next_chunk++]);
    }

    if (_png_check_iccp(&image->iccp)) {
        _png_addcapicity_tobytes(&chunk_size, &chunks);
        _png_write_chunk_iCCP(&image->iccp, &chunks[next_chunk++]);
    }

    if (_png_check_sbit(&image->sbit)) {
        _png_addcapicity_tobytes(&chunk_size, &chunks);
        _png_write_chunk_sBIT(&image->sbit, &chunks[next_chunk++]);
    }

    if (image->srgb.rendering < 4) {
        _png_addcapicity_tobytes(&chunk_size, &chunks);
        _png_write_chunk_sRGB(&image->srgb, &chunks[next_chunk++]);
    }

    for (uint32_t i = 0; i < image->textual_list.size; i++) {
        _png_addcapicity_tobytes(&chunk_size, &chunks);

        struct png_textual_data* textual = &image->textual_list.list[i];
        if (textual->type == PNG_TEXTUAL_UNCOMPRESSED) {
            _png_write_chunk_tEXt(&textual->data.text, &chunks[next_chunk++]);
        } else if (textual->type == PNG_TEXTUAL_COMPRESSED) {
            _png_write_chunk_zTXt(&textual->data.ztxt, &chunks[next_chunk++]);
        } else if (textual->type == PNG_TEXTUAL_INTERNATIONAL) {
            _png_write_chunk_iTXt(&textual->data.itxt, &chunks[next_chunk++]);
        }
    }

    uint8_t color = image->ihdr.color;
    int requiresPallete = color == 3 || ((color == 2 || color == 6) && image->plte.size > 0) ? 1 : 0;
    if (requiresPallete != 0) {
        _png_addcapicity_tobytes(&chunk_size, &chunks);
        _png_write_chunk_PLTE(&image->plte, &chunks[next_chunk++]);
    }

    if (image->trns.size > 0) {
        _png_addcapicity_tobytes(&chunk_size, &chunks);

        enum image_png_trns_type trns_type = image->trns.type;
        _png_convert_chunk_tRNS(&image->trns, PNG_tRNS_8BITS);
        _png_write_chunk_tRNS(&image->trns, &chunks[next_chunk++]);
        _png_convert_chunk_tRNS(&image->trns, trns_type);
    }

    if (_png_check_time(&image->time) != 0) {
        _png_addcapicity_tobytes(&chunk_size, &chunks);
        _png_write_chunk_tIME(&image->time, &chunks[next_chunk++]);
    }

    _png_convert_chunk_IDAT(&image->ihdr, &image->idat, PNG_IDAT_SCANLINES);
    _png_write_chunk_IDAT(&image->idat, &chunks[next_chunk++]);
    _png_convert_chunk_IDAT(&image->ihdr, &image->idat, PNG_IDAT_PIXELS);

    struct image_png_chunk* iend = &chunks[next_chunk];
    iend->length = 0;
    strcpy(iend->type, "IEND");
    iend->data = NULL;
    iend->crc = MEDIA_CRC32(media_update_crc32(MEDIA_CRC32_DEFAULT, (uint8_t*) "IEND", 4));

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
    if (image->trns.type == PNG_tRNS_8BITS) {
        free(image->trns.data_8bits);
    } else if (image->trns.type == PNG_tRNS_16BITS) {
        free(image->trns.data_16bits);
    }

    for (size_t i = 0; i < image->textual_list.size; i++) {
        struct png_textual_data* textual = &image->textual_list.list[i];

        if (textual->type == PNG_TEXTUAL_UNCOMPRESSED) {
            free(textual->data.text.text);
        } else if (textual->type == PNG_TEXTUAL_COMPRESSED) {
            free(textual->data.ztxt.text);
        } else if (textual->type == PNG_TEXTUAL_INTERNATIONAL) {
            free(textual->data.itxt.language_tag);
            free(textual->data.itxt.translated_keyword);
            free(textual->data.itxt.text);
        }
    }
    free(image->textual_list.list);

    free(image->iccp.data);
    free(image->plte.pallete);
    free(image->idat.data);
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

static inline uint16_t convert_short_be(uint16_t value) {
    if (media_actual_endian() == MEDIA_BIG_ENDIAN) {
        // no need to shift
        return value;
    }

    uint8_t* bytes = (uint8_t *) &value;
    return bytes[0] << 8 | bytes[1];
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
    if (dest->data == NULL) {
        dest->data = malloc(sizeof(uint8_t) * src->length);
    } else {
        dest->data = realloc(dest->data, sizeof(uint8_t) * src->length); 
    }
    memcpy(dest->data, src->data, sizeof(uint8_t) * src->length);
    dest->crc = src->crc;
}

static inline uint32_t _png_get_chunk_crc32(struct image_png_chunk* chunk) {
    uint32_t crc = MEDIA_CRC32_DEFAULT;
    crc = media_update_crc32(crc, (uint8_t *) chunk->type, 4);
    crc = media_update_crc32(crc, chunk->data, chunk->length);

    return MEDIA_CRC32(crc);
}

static inline void _png_generate_crc32(struct image_png_chunk* chunk) {
    chunk->crc = _png_get_chunk_crc32(chunk); 
}

static inline int _png_check_crc32(struct image_png_chunk* chunk) {
    return chunk->crc != _png_get_chunk_crc32(chunk);
}

static inline void _png_populate_chunk(struct image_png_chunk* chunk, size_t bytes) {
    if (chunk->data == NULL) {
        chunk->data = malloc(bytes);
    } else {
        chunk->data = realloc(chunk->data, bytes);
    }
}

static int _png_read_chunk_IHDR(struct image_png_chunk* chunk, struct image_png_chunk_IHDR* ihdr) {
    if (chunk == NULL || chunk->length != 13 || strcmp(chunk->type, "IHDR") != 0) {
        return 1;
    }

    if (_png_check_crc32(chunk)) {
        return 2;
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

static int _png_read_chunk_PLTE(struct image_png_chunk* chunk, struct image_png_chunk_PLTE* plte) {
    if (chunk == NULL || strcmp(chunk->type, "PLTE") != 0 || chunk->length % 3 != 0 || chunk->length / 3 > 256) {
        return 1;
    }

    if (_png_check_crc32(chunk)) {
        return 2;
    }

    if (plte == NULL) {
        return 0;
    }

    plte->size = chunk->length / 3;

    plte->pallete = malloc(sizeof(struct image_color) * plte->size);
    for (uint16_t i = 0; i < plte->size; i++) {
        struct image_color* color = &plte->pallete[i];

        color->type = IMAGE_RGBA8_COLOR;
        color->rgba8.red = chunk->data[i * 3];
        color->rgba8.green = chunk->data[i * 3 + 1];
        color->rgba8.blue = chunk->data[i * 3 + 2];
        color->rgba8.alpha = 0;
    }

    return 0;
}

static int _png_read_chunk_tRNS(struct image_png_chunk* chunk, struct image_png_chunk_tRNS* trns) {
    if (chunk == NULL || strcmp(chunk->type, "tRNS") != 0) {
        return 1;
    }

    if (_png_check_crc32(chunk)) {
        return 2;
    }

    if (trns == NULL) {
        return 0;
    }

    // free some pointer if trns has some size
    if (trns->size > 0) {
        if (trns->type == PNG_tRNS_8BITS) {
            free(trns->data_8bits);
        } else if (trns->type == PNG_tRNS_16BITS) {
            free(trns->data_16bits);
        }
    }

    trns->type = PNG_tRNS_8BITS;
    trns->size = chunk->length;
    trns->data_8bits = malloc(sizeof(uint8_t) * trns->size);
    memcpy(trns->data_8bits, chunk->data, chunk->length);

    return 0;
}

static int _png_read_chunk_cHRM(struct image_png_chunk* chunk, struct image_png_chunk_cHRM* chrm) {
    if (chunk == NULL || strcmp(chunk->type, "cHRM") != 0 || chunk->length != 32) {
        return 1;
    }

    if (_png_check_crc32(chunk)) {
        return 2;
    }

    if (chrm == NULL) {
        return 0;
    }

    memcpy(chrm, chunk->data, sizeof(uint8_t) * 32);
    chrm->white_px = convert_int_be(chrm->white_px);
    chrm->white_py = convert_int_be(chrm->white_py);
    chrm->red_x = convert_int_be(chrm->red_x);
    chrm->red_y = convert_int_be(chrm->red_y);
    chrm->green_x = convert_int_be(chrm->green_x);
    chrm->green_y = convert_int_be(chrm->green_y);
    chrm->blue_x = convert_int_be(chrm->blue_x);
    chrm->blue_y = convert_int_be(chrm->blue_y);

    return 0;
}

static int _png_read_chunk_gAMA(struct image_png_chunk* chunk, struct image_png_chunk_gAMA* gama) {
    if (chunk == NULL || strcmp(chunk->type, "gAMA") != 0 || chunk->length != 4) {
        return 1;
    }

    if (_png_check_crc32(chunk)) {
        return 2;
    }

    if (gama == NULL) {
        return 0;
    }

    memcpy(gama, chunk->data, sizeof(uint8_t) * 4);
    gama->gamma = convert_int_be(gama->gamma);

    return 0;
}

static int _png_read_chunk_iCCP(struct image_png_chunk* chunk, struct image_png_chunk_iCCP* iccp) {
    if (chunk == NULL || strcmp(chunk->type, "iCCP") != 0) {
        return 1;
    }

    if (_png_check_crc32(chunk)) {
        return 2;
    }

    if (iccp == NULL) {
        return 0;
    }

    strncpy(iccp->name, (const char*) chunk->data, 80);
    size_t name_length = strlen(iccp->name) + 1;
    iccp->compression = chunk->data[name_length];
    iccp->size = chunk->length - name_length - 1;
    iccp->data = malloc(sizeof(uint8_t) * iccp->size);
    memcpy(iccp->data, &chunk->data[name_length + 1], iccp->size);

    return 0;
}

static int _png_read_chunk_sBIT(struct image_png_chunk* chunk, struct image_png_chunk_sBIT* sbit) {
    if (chunk == NULL || strcmp(chunk->type, "sBIT") != 0) {
        return 1;
    }

    if (_png_check_crc32(chunk)) {
        return 2;
    }

    if (sbit == NULL) {
        return 0;
    }

    switch (chunk->length) {
        case 1: {
            sbit->type = PNG_sBIT_GREY;
            sbit->grey = chunk->data[0];
            break;
        }
        case 2: {
            sbit->type = PNG_sBIT_GREYALPHA;
            sbit->grey = chunk->data[0];
            sbit->alpha = chunk->data[1];
            break;
        }
        case 3: {
            sbit->type = PNG_sBIT_RGB_OR_INDEXED;
            sbit->red = chunk->data[0];
            sbit->green = chunk->data[1];
            sbit->blue = chunk->data[2];
            break;
        }
        case 4: {
            sbit->type = PNG_sBIT_RGBALPHA;
            sbit->red = chunk->data[0];
            sbit->green = chunk->data[1];
            sbit->blue = chunk->data[2];
            sbit->alpha = chunk->data[3];
            break;
        }
    }

    return 0;
}

static int _png_read_chunk_sRGB(struct image_png_chunk* chunk, struct image_png_chunk_sRGB* srgb) {
    if (chunk == NULL || strcmp(chunk->type, "sRGB") != 0 || chunk->length != 1) {
        return 1;
    }
    
    if (_png_check_crc32(chunk)) {
        return 2;
    }

    if (srgb == NULL) {
        return 0;
    }

    srgb->rendering = chunk->data[0];

    return 0;
}

static int _png_read_chunk_tEXt(struct image_png_chunk* chunk, struct image_png_chunk_tEXt* text) {
    if (chunk == NULL || strcmp(chunk->type, "tEXt") != 0) {
        return 1;
    }
    
    if (_png_check_crc32(chunk)) {
        return 2;
    }

    if (text == NULL) {
        return 0;
    }

    strncpy(text->keyword, (char*) chunk->data, 80);
    size_t size = chunk->length - strlen(text->keyword);

    if (text->text == NULL) {
        text->text = malloc(sizeof(char) * size);
    } else {
        text->text = realloc(text->text, sizeof(char) * size); 
    }

    memcpy(text->text, &chunk->data[chunk->length - size + 1], size - 1);
    text->text[size - 1] = '\0';

    return 0;
}

static int _png_read_chunk_zTXt(struct image_png_chunk* chunk, struct image_png_chunk_zTXt* ztxt) {
    if (chunk == NULL || strcmp(chunk->type, "zTXt") != 0) {
        return 1;
    }

    if (_png_check_crc32(chunk)) {
        return 2;
    }

    if (ztxt == NULL) {
        return 0;
    }

    strncpy(ztxt->keyword, (char*) chunk->data, 80);
    size_t keyword_length = strlen(ztxt->keyword) + 1;
    ztxt->compression = chunk->data[keyword_length];

    size_t compressed_size = chunk->length - keyword_length - 1;
    size_t size;
    media_zlib_inflate(chunk->data + keyword_length + 1, compressed_size, (uint8_t**) &ztxt->text, &size);

    // prepare null-terminator
    ztxt->text = realloc(ztxt->text, sizeof(char) * (size + 1));
    ztxt->text[size] = '\0';

    return 0;
}

static int _png_read_chunk_iTXt(struct image_png_chunk* chunk, struct image_png_chunk_iTXt* itxt) {
    if (chunk == NULL || strcmp(chunk->type, "iTXt") != 0) {
        return 1;
    }

    if (_png_check_crc32(chunk)) {
        return 2;
    }

    if (itxt == NULL) {
        return 0;
    }

    size_t next = 0;

    strncpy(itxt->keyword, (char*) chunk->data, 80);
    next += strlen(itxt->keyword) + 1;

    itxt->compression_flag = chunk->data[next++];
    itxt->compression_method = chunk->data[next++];

    itxt->language_tag = strdup((char*) chunk->data + next);
    next += strlen(itxt->language_tag) + 1;

    itxt->translated_keyword= strdup((char*) chunk->data + next);
    next += strlen(itxt->translated_keyword) + 1;

    itxt->text = strndup((char*) chunk->data + next, chunk->length - next);

    return 0;
}

static int _png_read_chunk_tIME(struct image_png_chunk* chunk, struct image_png_chunk_tIME* time) {
    if (chunk == NULL || strcmp(chunk->type, "tIME") != 0 || chunk->length != 7) {
        return 1;
    }
    
    if (_png_check_crc32(chunk)) {
        return 2;
    }

    if (time == NULL) {
        return 0;
    }

    memcpy(time, chunk->data, sizeof(uint8_t) * 7);
    time->year = convert_short_be(time->year);

    return 0;
}

static int _png_read_chunk_IDAT(struct image_png_chunk* chunk, struct image_png_chunk_IDAT* idat) {
    if (chunk == NULL || strcmp(chunk->type, "IDAT") != 0) {
        return 1;
    }

    if (_png_check_crc32(chunk)) {
        return 2;
    }

    if (idat == NULL) {
        return 0;
    }

    idat->type = PNG_IDAT_SCANLINES;
    media_zlib_inflate(chunk->data, chunk->length, &idat->data, &idat->size);

    return 0;
}

static void _png_write_chunk_IHDR(struct image_png_chunk_IHDR* ihdr, struct image_png_chunk* chunk) {
    chunk->length = 13;
    strcpy(chunk->type, "IHDR");
    _png_populate_chunk(chunk, sizeof(uint8_t) * 13);

    ihdr->width = convert_int_be(ihdr->width);
    ihdr->height = convert_int_be(ihdr->height);

    memcpy(chunk->data, ihdr, sizeof(uint8_t) * 13);

    ihdr->width = convert_int_be(ihdr->width);
    ihdr->height = convert_int_be(ihdr->height);

    _png_generate_crc32(chunk);
}

static void _png_write_chunk_PLTE(struct image_png_chunk_PLTE* plte, struct image_png_chunk* chunk) {
    uint16_t size = plte->size > 256 ? 256 : plte->size;
    chunk->length = size * 3;
    strcpy(chunk->type, "PLTE");
    _png_populate_chunk(chunk, sizeof(uint8_t) * chunk->length);

    for (uint16_t i = 0; i < size; i++) {
        struct image_color* color = &plte->pallete[i];

        chunk->data[i * 3] = color->rgba8.red;
        chunk->data[i * 3 + 1] = color->rgba8.green;
        chunk->data[i * 3 + 2] = color->rgba8.blue;
    }

    _png_generate_crc32(chunk);
}

static void _png_write_chunk_tRNS(struct image_png_chunk_tRNS* trns, struct image_png_chunk* chunk) {
    chunk->length = trns->size;
    strcpy(chunk->type, "tRNS");
    _png_populate_chunk(chunk, sizeof(uint8_t) * chunk->length);

    memcpy(chunk->data, trns->data_8bits, sizeof(uint8_t) * chunk->length);

    _png_generate_crc32(chunk);
}

static void _png_write_chunk_cHRM(struct image_png_chunk_cHRM* chrm, struct image_png_chunk* chunk) {
    chunk->length = 32;
    strcpy(chunk->type, "cHRM");
    _png_populate_chunk(chunk, sizeof(uint8_t) * 32);

    chrm->white_px = convert_int_be(chrm->white_px);
    chrm->white_py = convert_int_be(chrm->white_py);
    chrm->red_x = convert_int_be(chrm->red_x);
    chrm->red_y = convert_int_be(chrm->red_y);
    chrm->green_x = convert_int_be(chrm->green_x);
    chrm->green_y = convert_int_be(chrm->green_y);
    chrm->blue_x = convert_int_be(chrm->blue_x);
    chrm->blue_y = convert_int_be(chrm->blue_y);

    memcpy(chunk->data, chrm, sizeof(uint8_t) * 32);

    chrm->white_px = convert_int_be(chrm->white_px);
    chrm->white_py = convert_int_be(chrm->white_py);
    chrm->red_x = convert_int_be(chrm->red_x);
    chrm->red_y = convert_int_be(chrm->red_y);
    chrm->green_x = convert_int_be(chrm->green_x);
    chrm->green_y = convert_int_be(chrm->green_y);
    chrm->blue_x = convert_int_be(chrm->blue_x);
    chrm->blue_y = convert_int_be(chrm->blue_y);

    _png_generate_crc32(chunk);
}

static void _png_write_chunk_gAMA(struct image_png_chunk_gAMA* gama, struct image_png_chunk* chunk) {
    chunk->length = 4;
    strcpy(chunk->type, "gAMA");
    _png_populate_chunk(chunk, sizeof(uint8_t) * 4);

    gama->gamma = convert_int_be(gama->gamma);
    memcpy(chunk->data, gama, sizeof(uint8_t) * 4);
    gama->gamma = convert_int_be(gama->gamma);

    _png_generate_crc32(chunk);
}

static void _png_write_chunk_iCCP(struct image_png_chunk_iCCP* iccp, struct image_png_chunk* chunk) {
    chunk->length = strlen(iccp->name) + iccp->size + 2;
    strcpy(chunk->type, "iCCP");
    _png_populate_chunk(chunk, sizeof(uint8_t) * chunk->length);

    strncpy((char*) chunk->data, iccp->name, chunk->length - iccp->size - 1);
    size_t name_length = strlen(iccp->name) + 1;
    chunk->data[name_length] = iccp->compression;
    memcpy(&chunk->data[name_length + 1], iccp->data, iccp->size);

    _png_generate_crc32(chunk);
}

static void _png_write_chunk_sBIT(struct image_png_chunk_sBIT* sbit, struct image_png_chunk* chunk) {
    strcpy(chunk->type, "sBIT");

    switch (sbit->type) {
        case PNG_sBIT_GREY:
        case PNG_sBIT_GREYALPHA: {
            chunk->length = sbit->type == PNG_sBIT_GREYALPHA ? 2 : 1;
            _png_populate_chunk(chunk, sizeof(uint8_t) * chunk->length);

            chunk->data[0] = sbit->grey;
            if (sbit->type == PNG_sBIT_GREYALPHA) {
                chunk->data[1] = sbit->alpha;
            }

            break;
        }
        case PNG_sBIT_RGB_OR_INDEXED:
        case PNG_sBIT_RGBALPHA: {
            chunk->length = sbit->type == PNG_sBIT_RGBALPHA ? 4 : 3;
            _png_populate_chunk(chunk, sizeof(uint8_t) * chunk->length);

            chunk->data[0] = sbit->red;
            chunk->data[1] = sbit->green;
            chunk->data[2] = sbit->blue;
            if (sbit->type == PNG_sBIT_RGBALPHA) {
                chunk->data[3] = sbit->alpha;
            }

            break;
        }
    }

    _png_generate_crc32(chunk);
}

static void _png_write_chunk_sRGB(struct image_png_chunk_sRGB* srgb, struct image_png_chunk* chunk) {
    chunk->length = 1;
    strcpy(chunk->type, "sRGB");
    _png_populate_chunk(chunk, sizeof(uint8_t));

    chunk->data[0] = srgb->rendering;

    _png_generate_crc32(chunk);
}

static void _png_write_chunk_tEXt(struct image_png_chunk_tEXt* text, struct image_png_chunk* chunk) {
    // null-terminator should not be included
    size_t size = strlen(text->text);
    chunk->length = strlen(text->keyword) + size + 1;
    strcpy(chunk->type, "tEXt");
    _png_populate_chunk(chunk, sizeof(uint8_t) * chunk->length);

    memcpy(chunk->data, text->keyword, chunk->length - size);
    memcpy(&chunk->data[chunk->length - size], text->text, size);

    _png_generate_crc32(chunk);
}

static void _png_write_chunk_zTXt(struct image_png_chunk_zTXt* ztxt, struct image_png_chunk* chunk) {
    chunk->length = strlen(ztxt->keyword) + 2;
    strcpy(chunk->type, "zTXt");
    _png_populate_chunk(chunk, sizeof(uint8_t) * chunk->length);

    strncpy((char*) chunk->data, ztxt->keyword, chunk->length - 1);
    size_t keyword_length = chunk->length - 1;
    chunk->data[keyword_length] = ztxt->compression;

    uint8_t* compressed;
    size_t compressed_size;
    // null terminator is not included in this deflate
    media_zlib_deflate((uint8_t*) ztxt->text, strlen(ztxt->text), &compressed, &compressed_size, Z_BEST_COMPRESSION);

    chunk->length += compressed_size;
    _png_populate_chunk(chunk, sizeof(uint8_t) * chunk->length);
    memcpy(&chunk->data[keyword_length + 1], compressed, compressed_size);
    
    free(compressed);

    _png_generate_crc32(chunk);
}

static void _png_write_chunk_iTXt(struct image_png_chunk_iTXt* itxt, struct image_png_chunk* chunk) {
    size_t keyword_length = strlen(itxt->keyword) + 1;
    size_t language_tag_length = strlen(itxt->language_tag) + 1;
    size_t translated_keyword_length = strlen(itxt->translated_keyword) + 1;
    size_t text_length = strlen(itxt->text);

    chunk->length = keyword_length + language_tag_length + translated_keyword_length + text_length + 2;
    strcpy(chunk->type, "iTXt");
    _png_populate_chunk(chunk, sizeof(uint8_t) * chunk->length);

    size_t next = 0;

    strncpy((char*) chunk->data, itxt->keyword, keyword_length);
    next += keyword_length;

    chunk->data[next++] = itxt->compression_flag;
    chunk->data[next++] = itxt->compression_method;

    memcpy(chunk->data + next, itxt->language_tag, language_tag_length);
    next += language_tag_length;

    memcpy(chunk->data + next, itxt->translated_keyword, translated_keyword_length);
    next += translated_keyword_length;

    memcpy(chunk->data + next, itxt->text, text_length);

    _png_generate_crc32(chunk);
}

static void _png_write_chunk_tIME(struct image_png_chunk_tIME* time, struct image_png_chunk* chunk) {
    chunk->length = 7;
    strcpy(chunk->type, "tIME");
    _png_populate_chunk(chunk, sizeof(uint8_t) * 7);

    time->year = convert_short_be(time->year);
    memcpy(chunk->data, time, sizeof(uint8_t) * 7);
    time->year = convert_short_be(time->year);

    _png_generate_crc32(chunk);
}

static void _png_write_chunk_IDAT(struct image_png_chunk_IDAT* idat, struct image_png_chunk* chunk) {
    strcpy(chunk->type, "IDAT");

    if (chunk->data != NULL) {
        free(chunk->data);
    }

    size_t length;
    media_zlib_deflate(idat->data, idat->size, &chunk->data, &length, Z_BEST_COMPRESSION);
    chunk->length = length;

    _png_generate_crc32(chunk);
}

static void _png_convert_chunk_tRNS(struct image_png_chunk_tRNS* trns,
                                    enum image_png_trns_type type) {
    // nothing to do
    if (trns->type == type) {
        return;
    }

    switch (type) {
        case PNG_tRNS_8BITS: {
            uint8_t* data_8bits = malloc(sizeof(uint8_t) * trns->size * 2);

            for (size_t i = 0; i < trns->size; i++) {
                uint16_t byte = convert_int_be(trns->data_16bits[i]);

                data_8bits[i * 2] = byte;
                data_8bits[i * 2 + 1] = (byte >> 8) & 0xFF;
            }

            free(trns->data_16bits);
            trns->data_8bits = data_8bits;
            break;
        }
        case PNG_tRNS_16BITS: {
            uint16_t* data_16bits = malloc(sizeof(uint16_t) * trns->size / 2);

            for (size_t i = 0; i < trns->size; i++) {
                data_16bits[i] = convert_int_be(trns->data_8bits[i * 2] | (trns->data_8bits[i * 2 + 1] >> 8));
            }
            
            free(trns->data_8bits);
            trns->data_16bits = data_16bits;
            break;
        }
    }
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

static inline enum image_png_sbit_type _png_color_to_sbit(uint8_t color) {
    switch (color) {
        case 0: return PNG_sBIT_GREY;
        case 2:
        case 3: return PNG_sBIT_RGB_OR_INDEXED;
        case 4: return PNG_sBIT_GREYALPHA;
        case 6: return PNG_sBIT_RGBALPHA;
    }

    return PNG_sBIT_GREY;
}

static inline int _png_check_chrm(struct image_png_chunk_cHRM* chrm) {
    return chrm->white_px != 0 || chrm->white_py != 0 || chrm->red_x != 0 || chrm->red_y != 0
        || chrm->green_x != 0 || chrm->green_y != 0 || chrm->blue_x != 0 || chrm->blue_y != 0;
}

static inline int _png_check_iccp(struct image_png_chunk_iCCP* iccp) {
    return strcmp(iccp->name, "") != 0 || iccp->size > 0;
}

static inline int _png_check_sbit(struct image_png_chunk_sBIT* sbit) {
    switch (sbit->type) {
        case PNG_sBIT_GREY: return sbit->grey != 0;
        case PNG_sBIT_RGB_OR_INDEXED: return sbit->red != 0 || sbit->green != 0 || sbit->blue != 0;
        case PNG_sBIT_GREYALPHA: return sbit->grey != 0 || sbit->alpha != 0;
        case PNG_sBIT_RGBALPHA: return sbit->red != 0 || sbit->green != 0 || sbit->blue != 0 || sbit->alpha != 0;
    }

    return 0;
}

static inline int _png_check_time(struct image_png_chunk_tIME* time) {
    return time->year != 0 || time->month != 0 || time->day != 0 || time->hour != 0 || time->minute != 0 || time->second != 0;
}

static void _png_add_text(struct png_textual_list* list,
                          struct png_textual_data* textual) {
    uint32_t index = list->size;
    list->size++;

    if (list->size == 1) {
        list->list = malloc(sizeof(struct png_textual_data));
    } else {
        list->list = realloc(list->list, sizeof(struct png_textual_data) * list->size);
    }

    memcpy(&list->list[index], textual, sizeof(struct png_textual_data));
}

static void _png_get_text(struct png_textual_list* list,
                                 const char* keyword,
                                 struct png_textual_data** out_textual,
                                 uint32_t* index) {
    for (uint32_t i = 0; i < list->size; i++) {
        struct png_textual_data* textual = &list->list[i];
        char* keyword;

        if (textual->type == PNG_TEXTUAL_UNCOMPRESSED) {
            keyword = textual->data.text.keyword;
        } else if (textual->type == PNG_TEXTUAL_COMPRESSED) {
            keyword = textual->data.ztxt.keyword;
        } else if (textual->type == PNG_TEXTUAL_INTERNATIONAL) {
            keyword = textual->data.itxt.keyword;
        }

        if (strcmp(keyword, keyword) != 0) {
            continue;
        }

        *out_textual = textual;
        if (index != NULL) {
            *index = i;
        }
        
        return;
    }

    *out_textual = NULL;
    if (index != NULL) {
        *index = 0;
    }
}

static void _png_sub_text(struct png_textual_list* list,
                                 const char* keyword) {
    struct png_textual_data* textual;
    uint32_t index;

    _png_get_text(list, keyword, &textual, &index);

    // not found, ignore it
    if (textual == NULL) {
        return;
    }

    switch (textual->type) {
        case PNG_TEXTUAL_UNCOMPRESSED: {
            free(textual->data.text.text);
            break;
        }
        case PNG_TEXTUAL_COMPRESSED: {
            free(textual->data.ztxt.text);
            break;
        }
        case PNG_TEXTUAL_INTERNATIONAL: {
            free(textual->data.itxt.language_tag);
            free(textual->data.itxt.translated_keyword);
            free(textual->data.itxt.text);
            break;
        }
    }

    list->size--;

    if (list->size == 0) {
        free(list->list);
        list->list = NULL; 
    } else if (index == list->size) {
        list->list = realloc(list->list, sizeof(struct png_textual_data) * list->size);
    } else {
        memmove(&list->list[index], &list->list[index + 1], sizeof(struct png_textual_data) * (list->size - index));
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
