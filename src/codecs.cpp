#include "imagefilter/image.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_PNM
#define STBI_MAX_DIMENSIONS 32768
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace imagefilter {

Image load_image(const std::string& filename) {
    int width = 0, height = 0, channels = 0;
    if (!stbi_info(filename.c_str(), &width, &height, &channels))
        throw std::runtime_error("cannot read image header: " + filename);
    Image::checked_size(width, height);
    using StbiPtr = std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>;
    StbiPtr pixels(stbi_load(filename.c_str(), &width, &height, &channels, 3), stbi_image_free);
    if (!pixels) throw std::runtime_error("cannot decode image: " + filename);
    Image result(width, height);
    std::memcpy(result.data(), pixels.get(), result.size());
    return result;
}

void save_image(const Image& image, const std::string& filename) {
    const auto dot = filename.find_last_of('.');
    std::string extension = dot == std::string::npos ? "" : filename.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    int ok = 0;
    if (extension == ".png")
        ok = stbi_write_png(filename.c_str(), image.width(), image.height(), 3,
                            image.data(), static_cast<int>(image.stride()));
    else if (extension == ".jpg" || extension == ".jpeg")
        ok = stbi_write_jpg(filename.c_str(), image.width(), image.height(), 3, image.data(), 95);
    else if (extension == ".bmp")
        ok = stbi_write_bmp(filename.c_str(), image.width(), image.height(), 3, image.data());
    else if (extension == ".tga")
        ok = stbi_write_tga(filename.c_str(), image.width(), image.height(), 3, image.data());
    else if (extension == ".ppm") {
        std::ofstream output(filename, std::ios::binary);
        output << "P6\n" << image.width() << ' ' << image.height() << "\n255\n";
        output.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
        output.close();
        ok = !output.fail();
    } else throw std::invalid_argument("output extension must be .png, .jpg, .jpeg, .bmp, .tga, or .ppm");
    if (!ok) throw std::runtime_error("cannot write image: " + filename);
}

} // namespace imagefilter
