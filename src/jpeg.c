#include "image.h"

#include <stdlib.h>

struct image_jpeg {

};

struct image_jpeg* image_jpeg_open(const char* path) {
    struct image_jpeg* image = malloc(sizeof(struct image_jpeg));

    return image;
}

void image_jpeg_close(struct image_jpeg *image) {
    free(image);
}
