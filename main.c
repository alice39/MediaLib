#include <stdio.h>
#include <stdlib.h>

#include "image.h"
#include "utils.h"

void test_sample() {
    struct image_png* image = image_png_open("sample.png");
    if (image == NULL) {
        perror("it could not open sample.png");
        return;
    }

    uint32_t width = image_png_get_width(image);
    uint32_t height = image_png_get_height(image);
    uint8_t color_type = image_png_get_color(image);
    uint8_t depth = image_png_get_depth(image);

    printf("sample.png:\n width: %d\n height: %d\n color: %d\n depth: %d\n", width, height, color_type, depth);

    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            struct image_color color;
            image_png_get_pixel(image, x, y, &color);
            if (color.type == IMAGE_RGBA16_COLOR) {
                printf("Pixel at %d,%d: T: %d RGBA: %02X%02X%02X%02X\n", x, y, (uint8_t) color.type, (uint8_t) color.rgba16.red, (uint8_t) color.rgba16.green, (uint8_t) color.rgba16.blue, (uint8_t) color.rgba16.alpha);
            } else if (color.type == IMAGE_RGBA8_COLOR) {
                printf("Pixel at %d,%d: T: %d RGBA: %02X%02X%02X%02X\n", x, y, color.type, color.rgba8.red, color.rgba8.green, color.rgba8.blue, color.rgba8.alpha);
            } else {
                printf("Error, no handling color type: %d\n", color.type);
            }
        }
    }

    image_png_close(image);
}

void test_empty_sample() {
    printf("Creating empty PNG\n");

    struct image_png* empty_image = image_png_create(IMAGE_RGBA8_COLOR, 2, 2);

    struct image_color color;
    color.rgba8.red = 0xFF;
    color.rgba8.green = 0x01;
    color.rgba8.blue = 0x01;
    color.rgba8.alpha = 0xFF;

    image_png_set_pixel(empty_image, 0, 0, color);

    image_png_save(empty_image, "empty_image.png");
    image_png_close(empty_image);
}

int main() {
    test_sample(); 
    test_empty_sample();

    return 0;
}
