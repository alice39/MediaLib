#include "image.hpp"

media::ImagePNG::ImagePNG(const std::string& path) {
    image = image_png_open(path.c_str());
}

media::ImagePNG::ImagePNG(image_color_type type, uint32_t width, uint32_t height) {
    image = image_png_create(type, width, height);
}

media::ImagePNG::~ImagePNG() {
    if (image != NULL) {
        image_png_close(image);
    }
}

bool media::ImagePNG::isLoaded() const {
    return image != NULL;
}

bool media::ImagePNG::open(const std::string& path) {
    if (image != NULL) {
        image_png_close(image);
    }

    image = image_png_open(path.c_str());
    return image != NULL;
}

image_dimension media::ImagePNG::getDimension() const {
    image_dimension dimension{0, 0};

    if (image != NULL) {
        image_png_get_dimension(image, &dimension);
    }

    return dimension;
}

void media::ImagePNG::setDimension(const image_dimension &dimension) {
    if (image == NULL) {
        return;
    }

    image_png_set_dimension(image, dimension);
}

image_color_type media::ImagePNG::getColor() const {
    image_color_type type;

    if (image != NULL) {
        image_png_get_color(image, &type);
    }

    return type;
}

void media::ImagePNG::setColor(image_color_type type) {
    if (image == NULL) {
        return;
    }

    image_png_set_color(image, type);
}

uint32_t media::ImagePNG::getGamma() const {
    uint32_t gamma = 0;

    if (image != NULL) {
        image_png_get_gamma(image, &gamma);
    }

    return gamma;
}

void media::ImagePNG::setGamma(uint32_t gamma) {
    if (image == NULL) {
        return;
    }

    image_png_set_gamma(image, gamma);
}

image_color media::ImagePNG::getSBIT() const {
    image_color color{};

    if (image != NULL) {
        image_png_get_sbit(image, &color);
    }

    return color;
}

void media::ImagePNG::setSBIT(const image_color& color) {
    if (image == NULL) {
        return;
    }

    image_png_set_sbit(image, color);
}

uint8_t media::ImagePNG::getSRGB() const {
    uint8_t srgb = 0;

    if (image != NULL) {
        image_png_get_srgb(image, &srgb);
    }

    return srgb;
}

void media::ImagePNG::setSRGB(uint8_t srgb) {
    if (image == NULL) {
        return;
    }

    image_png_set_srgb(image, srgb);
}

void media::ImagePNG::setText(const std::string& keyword, const std::string& text, int16_t compress) {
    if (image == NULL) {
        return;
    }

    image_png_set_text(image, keyword.c_str(), text.c_str(), compress);
}

void media::ImagePNG::setText(const std::string& keyword, int16_t compression_flag, int16_t compression_method,
                              const std::string& language_tag, const std::string& translated_keyword, const std::string& text) {
    if (image == NULL) {
        return;
    }

    image_png_set_itxt(image, keyword.c_str(), compression_flag, compression_method,
                       language_tag.c_str(), translated_keyword.c_str(), text.c_str());
}

std::tuple<std::string, int16_t> media::ImagePNG::getText(const std::string& keyword) const {
    std::string text{};
    int16_t compress = 0;

    if (image != NULL) {
        char* raw_text;
        image_png_get_text(image, keyword.c_str(), &raw_text, &compress);
    
        text = raw_text;
        free(raw_text);
    }

    return std::pair<std::string, int16_t>(text, compress);
}

std::tuple<int16_t, int16_t, std::string, std::string, std::string> media::ImagePNG::getItxt(const std::string& keyword) const{ 
    int16_t compression_flag = 0;
    int16_t compression_method = 0;
    std::string language_tag{};
    std::string translated_keyword{};
    std::string text{};

    if (image != NULL) {
        char* raw_language_tag;
        char* raw_translated_keyword;
        char* raw_text;
        image_png_get_itxt(image, keyword.c_str(), &compression_flag, &compression_method, &raw_language_tag, &raw_translated_keyword, &raw_text);

        language_tag = raw_language_tag;
        translated_keyword = raw_translated_keyword;
        text = raw_text;

        free(raw_language_tag);
        free(raw_translated_keyword);
        free(raw_text);
    }

    return std::make_tuple(compression_flag, compression_method, language_tag, translated_keyword, text);
}

std::vector<std::string> media::ImagePNG::getKeys() {
    std::vector<std::string> keys{};

    if (image != NULL) {
        char** keywords;
        uint32_t size;
        image_png_get_keys(image, &keywords, &size);

        for (uint32_t i = 0; i < size; i++) {
            char* keyword = keywords[i];
            keys.emplace_back(keyword);
            free(keyword);
        }
        free(keywords);
    }

    return keys;
}

void media::ImagePNG::delText(const std::string& keyword) {
    if (image == NULL) {
        return;
    }

    image_png_del_text(image, keyword.c_str());
}

std::vector<image_color> media::ImagePNG::getPalette() const {
    std::vector<image_color> palette{};

    if (image != NULL) {
        image_color* raw_palette;
        uint16_t size;
        image_png_get_palette(image, &size, &raw_palette);

        for (uint16_t i = 0; i < size; i++) {
            palette.push_back(raw_palette[i]);
        }
        free(raw_palette);
    }

    return palette;
}

void media::ImagePNG::setPalette(std::vector<image_color> palette) {
    if (image == NULL) {
        return;
    }

    image_png_set_palette(image, palette.size(), palette.data());
}

image_color media::ImagePNG::getPixel(uint32_t x, uint32_t y) const {
    image_color color{};

    if (image != NULL) {
        image_png_get_pixel(image, x, y, &color);
    }

    return color;
}

void media::ImagePNG::setPixel(uint32_t x, uint32_t y, const image_color& color) {
    if (image == NULL) {
        return;
    }

    image_png_set_pixel(image, x, y, color);
}

image_time media::ImagePNG::getTimestamp() const {
    image_time time{};

    if (image != NULL) {
        image_png_get_timestamp(image, &time);
    }

    return time;
}

void media::ImagePNG::setTimestamp(const image_time& time) {
    if (image == NULL) {
        return;
    }

    image_png_set_timestamp(image, time);
}

std::tuple<std::unique_ptr<uint8_t[]>, size_t> media::ImagePNG::toBytes() const {
    if (image == NULL) {
        return std::pair<std::unique_ptr<uint8_t[]>, size_t>(std::make_unique<uint8_t[]>(0), 0);
    }

    uint8_t* raw_bytes;
    uint32_t raw_size;
    image_png_tobytes(image, &raw_bytes, &raw_size);

    std::unique_ptr<uint8_t[]> bytes = std::make_unique<uint8_t[]>(raw_size);
    for (uint32_t i = 0; i < raw_size; i++) bytes[i] = raw_bytes[i];
    return std::pair<std::unique_ptr<uint8_t[]>, size_t>(std::move(bytes), raw_size);
}

void media::ImagePNG::save(const std::string& path) const {
    if (image == NULL) {
        return;
    }

    image_png_save(image, path.c_str());
}

media::ImagePNG& media::ImagePNG::operator=(const ImagePNG& image) {
    if (this != &image) {
        if (this->image != NULL) {
            image_png_close(this->image);
        }

        this->image = image_png_copy(image.image);
    }

    return *this;
}

image_color media::generate_color16(uint16_t red, uint16_t green, uint16_t blue, uint16_t alpha) {
    image_color color{};

    color.type = static_cast<image_color_type>(static_cast<int>(IMAGE_RGBA16_COLOR) | IMAGE_ALPHA_BIT);
    color.rgba16.red = red;
    color.rgba16.green = green;
    color.rgba16.blue = blue;
    color.rgba16.alpha = alpha;

    return color;
}

image_color media::generate_color16(uint16_t red, uint16_t green, uint16_t blue) {
    image_color color{};

    color.type = IMAGE_RGBA16_COLOR;
    color.rgba16.red = red;
    color.rgba16.green = green;
    color.rgba16.blue = blue;

    return color;
}

image_color media::generate_color16(uint16_t grey, uint16_t alpha) {
    image_color color{};

    color.type = static_cast<image_color_type>(static_cast<int>(IMAGE_GRAY16_COLOR) | IMAGE_ALPHA_BIT);
    color.ga16.gray = grey;
    color.ga16.alpha = alpha;

    return color;
}

image_color media::generate_color16(uint16_t grey) {
    image_color color{};

    color.type = IMAGE_GRAY16_COLOR;
    color.ga16.gray = grey;

    return color;
}

image_color media::generate_color8(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha) {
    image_color color{};

    color.type = static_cast<image_color_type>(static_cast<int>(IMAGE_RGBA8_COLOR) | IMAGE_ALPHA_BIT);
    color.rgba8.red = red;
    color.rgba8.green = green;
    color.rgba8.blue = blue;
    color.rgba8.alpha = alpha;

    return color;
}

image_color media::generate_color8(uint8_t red, uint8_t green, uint8_t blue) {
    image_color color{};

    color.type = IMAGE_RGBA8_COLOR;
    color.rgba8.red = red;
    color.rgba8.green = green;
    color.rgba8.blue = blue;

    return color;
}

image_color media::generate_color8(uint8_t grey, uint8_t alpha) {
    image_color color{};

    color.type = static_cast<image_color_type>(static_cast<int>(IMAGE_GRAY8_COLOR) | IMAGE_ALPHA_BIT);
    color.ga8.gray = grey;
    color.ga8.alpha = alpha;

    return color;
}

image_color media::generate_color8(uint8_t grey) {
    image_color color{};

    color.type = IMAGE_GRAY8_COLOR;
    color.ga8.gray = grey;

    return color;
}

image_color media::generate_colori(uint8_t index) {
    image_color color{};

    color.type = IMAGE_INDEXED_COLOR;
    color.indexed = index;

    return color;
}
