#include "imagefilter/filters.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace imagefilter;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Function> void rejects(Function&& function) {
    bool rejected = false;
    try { function(); } catch (const std::exception&) { rejected = true; }
    require(rejected, "expected an exception");
}

void fill(Image& image, std::uint8_t value) {
    std::fill(image.data(), image.data() + image.size(), value);
}

// Independent, direct 2-D Gaussian reference (the implementation is separable).
Image reference_blur(const Image& image, int radius, double sigma) {
    Image result(image.width(), image.height());
    double total = 0;
    for (int dy = -radius; dy <= radius; ++dy)
        for (int dx = -radius; dx <= radius; ++dx)
            total += std::exp(-(dx * dx + dy * dy) / (2 * sigma * sigma));
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            for (int c = 0; c < 3; ++c) {
                double value = 0;
                for (int dy = -radius; dy <= radius; ++dy)
                    for (int dx = -radius; dx <= radius; ++dx)
                        value += image.row(std::clamp(y + dy, 0, image.height() - 1))[
                            std::clamp(x + dx, 0, image.width() - 1) * 3 + c] *
                            std::exp(-(dx * dx + dy * dy) / (2 * sigma * sigma)) / total;
                result.row(y)[x * 3 + c] = static_cast<std::uint8_t>(std::clamp(value + 0.5, 0.0, 255.0));
            }
    return result;
}

void image_limits() {
    rejects([] { Image image(0, 1); });
    rejects([] { Image image(-1, 10); });
    rejects([] { Image image(32769, 1); });
    rejects([] { Image image(32768, 32768); });
    require(Image::checked_size(32768, 1) == 32768u * 3, "valid limit rejected");
    require(generate_image(13, 17) == generate_image(13, 17), "generator is not deterministic");
}

void blur_reference() {
    const auto input = generate_image(13, 9);
    const auto expected = reference_blur(input, 3, 1.4);
    const auto output = gaussian_blur(input, 3, 1.4);
    for (std::size_t i = 0; i < output.size(); ++i)
        require(std::abs(static_cast<int>(output.data()[i]) - expected.data()[i]) <= 1,
                "blur differs from independent 2-D reference by more than one level");
    require(gaussian_blur(input, 0) == input, "radius-zero blur must be identity");
    Image flat(7, 5); fill(flat, 137);
    require(gaussian_blur(flat, 8) == flat, "blur changed constant image at boundaries");
    rejects([&] { gaussian_blur(input, -1); });
    rejects([&] { gaussian_blur(input, 129); });
    rejects([&] { gaussian_blur(input, 1, 0); });
    rejects([&] { gaussian_blur(input, 1, std::numeric_limits<double>::quiet_NaN()); });
}

void sobel_expected() {
    Image flat(5, 7); fill(flat, 100);
    const auto edges = sobel_edges(flat);
    require(std::all_of(edges.data(), edges.data() + edges.size(), [](auto c) { return c == 0; }),
            "constant image has edges");
    Image ramp(5, 3);
    for (int y = 0; y < 3; ++y) for (int x = 0; x < 5; ++x)
        std::fill(ramp.row(y) + x * 3, ramp.row(y) + x * 3 + 3, static_cast<std::uint8_t>(x * 10));
    const auto response = sobel_edges(ramp);
    for (int y = 0; y < 3; ++y) for (int x = 0; x < 5; ++x)
        for (int c = 0; c < 3; ++c)
            require(response.row(y)[x * 3 + c] == (x == 0 || x == 4 ? 40 : 80),
                    "unexpected Sobel response on linear ramp");
    Image step(3, 1); fill(step, 0);
    std::fill(step.row(0) + 6, step.row(0) + 9, 255);
    require(sobel_edges(step).row(0)[3] == 255, "strong edge did not saturate");
}

void resize_expected() {
    const auto source = generate_image(11, 7);
    require(resize_bilinear(source, 11, 7) == source, "same-size resize changed pixels");
    Image corners(2, 2);
    const int values[] = {0, 100, 150, 250};
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x)
        std::fill(corners.row(y) + x * 3, corners.row(y) + x * 3 + 3, static_cast<std::uint8_t>(values[y * 2 + x]));
    const auto larger = resize_bilinear(corners, 3, 3);
    const int expected[] = {0, 50, 100, 75, 125, 175, 150, 200, 250};
    for (int y = 0; y < 3; ++y) for (int x = 0; x < 3; ++x) for (int c = 0; c < 3; ++c)
        require(larger.row(y)[x * 3 + c] == expected[y * 3 + x], "bilinear reference mismatch");
    require(resize_bilinear(corners, 1, 1).data()[0] == 125, "2x2-to-1x1 average mismatch");
    rejects([&] { resize_bilinear(source, 0, 10); });
}

void degenerate_dimensions() {
    ThreadPool pool(4);
    for (const auto size : {std::pair<int, int>{1, 1}, {1, 19}, {17, 1}, {2, 2}}) {
        auto source = generate_image(size.first, size.second);
        require(gaussian_blur(source, 8) == gaussian_blur(source, 8, 2, &pool), "tiny blur mismatch");
        require(sobel_edges(source) == sobel_edges(source, &pool), "tiny Sobel mismatch");
        require(resize_bilinear(source, 3, 5) == resize_bilinear(source, 3, 5, &pool), "tiny resize mismatch");
        if (size.first == 1 && size.second == 1) {
            require(gaussian_blur(source, 8) == source, "single pixel blur changed color");
            const auto edge = sobel_edges(source);
            require(edge.data()[0] == 0 && edge.data()[1] == 0 && edge.data()[2] == 0, "single pixel edge must be zero");
        }
    }
}

void parallel_equality() {
    const auto input = generate_image(193, 131);
    const auto blur = gaussian_blur(input, 5, 1.7);
    const auto edge = sobel_edges(input);
    const auto resize = resize_bilinear(input, 87, 213);
    const auto combined = pipeline(input, 87, 71, 5, 1.7);
    for (int workers : {1, 2, 4, 10}) {
        ThreadPool pool(static_cast<std::size_t>(workers));
        for (int repetition = 0; repetition < 4; ++repetition) {
            require(gaussian_blur(input, 5, 1.7, &pool) == blur, "parallel blur differs");
            require(sobel_edges(input, &pool) == edge, "parallel edge differs");
            require(resize_bilinear(input, 87, 213, &pool) == resize, "parallel resize differs");
            require(pipeline(input, 87, 71, 5, 1.7, &pool) == combined, "parallel pipeline differs");
        }
    }
    require(input == generate_image(193, 131), "filter modified its input");
}

void pool_coverage_and_exceptions() {
    rejects([] { ThreadPool pool(0); });
    rejects([] { ThreadPool pool(257); });
    ThreadPool pool(4);
    std::vector<std::atomic<int>> visited(103);
    for (auto& value : visited) value.store(0);
    pool.parallel_rows(103, [&](int begin, int end) {
        for (int y = begin; y < end; ++y) ++visited[static_cast<std::size_t>(y)];
    }, 7);
    for (const auto& value : visited) require(value.load() == 1, "row omitted or processed twice");
    auto broken = pool.submit([] { throw std::runtime_error("test exception"); });
    rejects([&] { broken.get(); });
    rejects([&] { pool.parallel_rows(103, [](int, int) { throw std::runtime_error("row exception"); }); });
    std::atomic<int> after{0};
    pool.parallel_rows(73, [&](int begin, int end) { after += end - begin; });
    require(after.load() == 73, "pool cannot be reused after an exception");
    pool.parallel_rows(0, [](int, int) { throw std::runtime_error("empty range ran"); });
    rejects([&] { pool.parallel_rows(-1, [](int, int) {}); });
    rejects([&] { pool.parallel_rows(10, [](int, int) {}, 0); });
}

void pool_shutdown_drains() {
    std::atomic<int> completed{0};
    {
        ThreadPool pool(3);
        for (int i = 0; i < 100; ++i) pool.submit([&] { ++completed; });
    }
    require(completed.load() == 100, "destructor lost queued jobs");
}
} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"image limits and deterministic generator", image_limits},
        {"Gaussian blur against independent reference", blur_reference},
        {"Sobel constant, ramp and saturation", sobel_expected},
        {"bilinear interpolation reference", resize_expected},
        {"single-pixel and narrow images", degenerate_dimensions},
        {"serial/pool equality and input immutability", parallel_equality},
        {"pool coverage, exceptions and reuse", pool_coverage_and_exceptions},
        {"pool shutdown drains queued jobs", pool_shutdown_drains},
    };
    int failed = 0;
    for (const auto& test : tests) {
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << test.first << ": " << error.what() << '\n'; }
    }
    std::cout << tests.size() - static_cast<std::size_t>(failed) << '/' << tests.size() << " test groups passed\n";
    return failed ? 1 : 0;
}
