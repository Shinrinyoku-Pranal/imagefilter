#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace imagefilter {

// Packed, row-major, 8-bit RGB. Alpha is discarded on decoding.
class Image {
public:
    static constexpr int channels = 3;
    static constexpr std::size_t max_pixels = 64u * 1024u * 1024u;
    Image(int width, int height);
    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    std::size_t stride() const noexcept { return static_cast<std::size_t>(width_) * channels; }
    std::size_t size() const noexcept { return pixels_.size(); }
    std::uint8_t* data() noexcept { return pixels_.data(); }
    const std::uint8_t* data() const noexcept { return pixels_.data(); }
    std::uint8_t* row(int y) noexcept { return data() + static_cast<std::size_t>(y) * stride(); }
    const std::uint8_t* row(int y) const noexcept { return data() + static_cast<std::size_t>(y) * stride(); }
    bool operator==(const Image& other) const noexcept;
    std::uint64_t checksum() const noexcept;
    static std::size_t checked_size(int width, int height);
private:
    int width_;
    int height_;
    std::vector<std::uint8_t> pixels_;
};

Image load_image(const std::string& filename);
void save_image(const Image& image, const std::string& filename);
Image generate_image(int width, int height);

} // namespace imagefilter
