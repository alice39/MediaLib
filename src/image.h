#ifndef PNG_GUARD_HEADER
#define PNG_GUARD_HEADER

#include <stdint.h>

#define IMAGE_ALPHA_BIT 0x80

#define image_get_depth(type) ((type & 0x40) != 0 ? 16 : 8)

struct image_png;
struct image_jpeg;

// representation first 4 bits reserved to:
//  - alpha. See IMAGE_ALPHA_BIT
//  - 8 bits depth if zero or 16 bits if one
//  - and last 2 bits reserved to future usage
//
//  second 4 bits reserved to color type
enum image_color_type {
    IMAGE_RGBA8_COLOR   = 0x00,
    IMAGE_RGBA16_COLOR  = 0x41,

    IMAGE_GRAY8_COLOR   = 0x02,
    IMAGE_GRAY16_COLOR  = 0x43,

    IMAGE_INDEXED_COLOR = 0x04
};

struct image_color {
    enum image_color_type type;

    union {
        struct {
            uint8_t red;
            uint8_t green;
            uint8_t blue;
            uint8_t alpha; // it can be ignored
        } rgba8;

        struct {
            uint16_t red;
            uint16_t green;
            uint16_t blue;
            uint16_t alpha; // it can be ignored
        } rgba16;

        struct {
            uint8_t gray;
            uint8_t alpha; // it can be ignored
        } ga8;

        struct {
            uint16_t gray;
            uint16_t alpha; // it can be ignored 
        } ga16;

        uint8_t indexed;
    };
};

struct image_dimension {
    uint32_t width;
    uint32_t height;
};

struct image_png* image_png_create(enum image_color_type type, uint32_t width, uint32_t height);
struct image_png* image_png_open(const char* path);
void image_png_get_dimension(struct image_png* image, struct image_dimension* dimension);
// 0 if sucess otherise another number
int image_png_set_dimension(struct image_png* image, struct image_dimension dimension);
void image_png_get_color(struct image_png* image, enum image_color_type* type);
void image_png_set_color(struct image_png* image, enum image_color_type type);
void image_png_get_pixel(struct image_png* image, uint32_t x, uint32_t y, struct image_color* color);
void image_png_set_pixel(struct image_png* image, uint32_t x, uint32_t y, struct image_color color);
void image_png_tobytes(struct image_png* image, uint8_t** pbytes, uint32_t* psize);
void image_png_save(struct image_png* image, const char* path);
void image_png_close(struct image_png* image);

struct image_jpeg* image_jpeg_open(const char* path);
void image_jpeg_close(struct image_jpeg* image);

#endif // PNG_GUARD_HEADER
