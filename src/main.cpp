#include <iostream>
#include "image.hpp"

void test_empty_sample() {
    printf("Creating empty PNG C++\n");

    media::ImagePNG image(IMAGE_RGBA8_COLOR, 2, 2); 
    image.setPixel(0, 0, media::generate_color8(0xFF, 0x01, 0x01, 0xFF));

    image.save("empty_image_cpp.png");
}

int main() {
    test_empty_sample();

    return 0;
}
