#ifndef IMAGECPP_GUARD_HEADER
#define IMAGECPP_GUARD_HEADER

#include <cstdint>
#include <string>
#include <tuple>
#include <vector>
#include <memory>

extern "C" {
    #include "image.h"
}

namespace media {

    class ImagePNG {
        struct image_png* image;
        public:
        explicit ImagePNG(const std::string& path);
        ImagePNG(image_color_type type, uint32_t width, uint32_t height);
        ~ImagePNG();

        bool isLoaded() const;
        bool open(const std::string& path);

        image_dimension getDimension() const;
        void setDimension(const image_dimension& dimension);

        image_color_type getColor() const;
        void setColor(image_color_type type);

        uint32_t getGamma() const;
        void setGamma(uint32_t gamma);

        image_color getSBIT() const;
        void setSBIT(const image_color& sbit);

        uint8_t getSRGB() const;
        void setSRGB(uint8_t srgb);

        void setText(const std::string& keyword, const std::string& text, int16_t compress);
        void setText(const std::string& keyword, int16_t compression_flag, int16_t compression_method,
                     const std::string& language_tag, const std::string& translated_keyword, const std::string& text);
        std::tuple<std::string, int16_t> getText(const std::string& keyword) const;
        std::tuple<int16_t, int16_t, std::string, std::string, std::string> getItxt(const std::string& keyword) const;
        std::vector<std::string> getKeys();
        void delText(const std::string& keyword);

        std::vector<image_color> getPalette() const;
        void setPalette(std::vector<image_color> pallete);

        image_color getPixel(uint32_t x, uint32_t y) const;
        void setPixel(uint32_t x, uint32_t y, const image_color& color);

        image_time getTimestamp() const;
        void setTimestamp(const image_time& time);

        std::tuple<std::unique_ptr<uint8_t[]>, size_t> toBytes() const;
        void save(const std::string& path) const;

        ImagePNG& operator=(const ImagePNG& image);
    };

    image_color generate_color16(uint16_t red, uint16_t green, uint16_t blue, uint16_t alpha);
    image_color generate_color16(uint16_t red, uint16_t green, uint16_t blue);
    image_color generate_color16(uint16_t grey, uint16_t alpha);
    image_color generate_color16(uint16_t grey);

    image_color generate_color8(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha);
    image_color generate_color8(uint8_t red, uint8_t green, uint8_t blue);
    image_color generate_color8(uint8_t grey, uint8_t alpha);
    image_color generate_color8(uint8_t grey);

    image_color generate_colori(uint8_t index);
};

#endif // IMAGECPP_GUARD_HEADER
