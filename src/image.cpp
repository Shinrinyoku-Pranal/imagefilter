#include "imagefilter/image.hpp"

#include <stdexcept>

namespace imagefilter {

std::size_t Image::checked_size(int width, int height) {
    if (width <= 0 || height <= 0 || width > 32768 || height > 32768)
        throw std::invalid_argument("image dimensions must be between 1 and 32768");
    const auto pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (pixels > max_pixels) throw std::invalid_argument("image exceeds the 64-megapixel limit");
    return pixels * channels;
}

Image::Image(int width, int height)
    : width_(width), height_(height), pixels_(checked_size(width, height)) {}

bool Image::operator==(const Image& other) const noexcept {
    return width_ == other.width_ && height_ == other.height_ && pixels_ == other.pixels_;
}

std::uint64_t Image::checksum() const noexcept {
    std::uint64_t result = 14695981039346656037ull;
    for (auto byte : pixels_) {
        result ^= byte;
        result *= 1099511628211ull;
    }
    return result;
}

Image generate_image(int width, int height) {
    Image image(width, height);
    // Reproducible gradients, high-frequency noise, checkerboard and a disk.
    // Independent of std::random implementation and host endianness.
    std::uint32_t state = 0x12345678u;
    const double radius = static_cast<double>(width < height ? width : height) / 4.0;
    for (int y = 0; y < height; ++y) {
        auto* row = image.row(y);
        for (int x = 0; x < width; ++x) {
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            const int noise = static_cast<int>(state & 31u);
            const double dx = x - width / 2.0, dy = y - height / 2.0;
            const bool disk = dx * dx + dy * dy < radius * radius;
            const int checker = ((x / 48 + y / 48) & 1) * 64;
            row[x * 3] = static_cast<std::uint8_t>((x * 191 / width + noise + checker) % 256);
            row[x * 3 + 1] = static_cast<std::uint8_t>((y * 191 / height + noise + (disk ? 64 : 0)) % 256);
            row[x * 3 + 2] = static_cast<std::uint8_t>((checker + noise + (disk ? 128 : 48)) % 256);
        }
    }
    return image;
}

} // namespace imagefilter
