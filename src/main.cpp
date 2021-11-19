#include <iostream>
#include "image.hpp"

void test_empty_sample() {
    printf("Creating empty PNG C++\n");

    media::ImagePNG image(IMAGE_RGBA8_COLOR, 2, 2); 

    image_color color;
    color.rgba8.red = 0xFF;
    color.rgba8.green = 0x01;
    color.rgba8.blue = 0x01;
    color.rgba8.alpha = 0xFF;

    image.setPixel(0, 0, color);

    image.save("empty_image_cpp.png");
}

int main() {
    test_empty_sample();

    return 0;
}
