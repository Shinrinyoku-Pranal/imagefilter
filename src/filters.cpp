#include "imagefilter/filters.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace imagefilter {
namespace {

template<class Function>
void rows(int count, ThreadPool* pool, Function&& function) {
    if (pool) pool->parallel_rows(count, std::forward<Function>(function));
    else function(0, count);
}

std::uint8_t byte(float value) {
    return static_cast<std::uint8_t>(std::clamp(value + 0.5f, 0.0f, 255.0f));
}

struct Sample { int first; int second; float weight; };

std::vector<Sample> samples(int source, int destination) {
    std::vector<Sample> result(static_cast<std::size_t>(destination));
    for (int i = 0; i < destination; ++i) {
        const double position = std::clamp((i + 0.5) * source / destination - 0.5,
                                           0.0, static_cast<double>(source - 1));
        const int first = static_cast<int>(position);
        result[static_cast<std::size_t>(i)] = {first, std::min(first + 1, source - 1),
                                              static_cast<float>(position - first)};
    }
    return result;
}

} // namespace

Image gaussian_blur(const Image& input, int radius, double sigma, ThreadPool* pool) {
    if (radius < 0 || radius > 128) throw std::invalid_argument("blur radius must be between 0 and 128");
    if (!std::isfinite(sigma) || sigma < 0.01 || sigma > 128.0)
        throw std::invalid_argument("blur sigma must be finite and between 0.01 and 128");
    if (radius == 0) return input;
    std::vector<float> weights(static_cast<std::size_t>(2 * radius + 1));
    double sum = 0;
    for (int k = -radius; k <= radius; ++k) {
        const double weight = std::exp(-static_cast<double>(k * k) / (2.0 * sigma * sigma));
        weights[static_cast<std::size_t>(k + radius)] = static_cast<float>(weight);
        sum += weight;
    }
    for (auto& weight : weights) weight = static_cast<float>(weight / sum);
    const int width = input.width(), height = input.height();
    const auto stride = input.stride();
    std::vector<float> horizontal(input.size());
    Image output(width, height);

    rows(height, pool, [&](int begin, int end) {
        for (int y = begin; y < end; ++y) {
            const auto* source = input.row(y);
            auto* target = horizontal.data() + static_cast<std::size_t>(y) * stride;
            for (int x = 0; x < width; ++x) {
                float red = 0, green = 0, blue = 0;
                for (int k = -radius; k <= radius; ++k) {
                    const auto offset = static_cast<std::size_t>(std::clamp(x + k, 0, width - 1)) * 3;
                    const float weight = weights[static_cast<std::size_t>(k + radius)];
                    red += source[offset] * weight;
                    green += source[offset + 1] * weight;
                    blue += source[offset + 2] * weight;
                }
                target[x * 3] = red; target[x * 3 + 1] = green; target[x * 3 + 2] = blue;
            }
        }
    }); // barrier: all horizontal rows exist before the vertical pass

    rows(height, pool, [&](int begin, int end) {
        for (int y = begin; y < end; ++y) {
            auto* target = output.row(y);
            // Row pointers avoid recomputing the vertical boundary per channel.
            const float* neighbors[257];
            for (int k = -radius; k <= radius; ++k)
                neighbors[k + radius] = horizontal.data() +
                    static_cast<std::size_t>(std::clamp(y + k, 0, height - 1)) * stride;
            for (std::size_t x = 0; x < stride; ++x) {
                float value = 0;
                for (int k = 0; k <= radius * 2; ++k)
                    value += neighbors[k][x] * weights[static_cast<std::size_t>(k)];
                target[x] = byte(value);
            }
        }
    });
    return output;
}

Image sobel_edges(const Image& input, ThreadPool* pool) {
    const int width = input.width(), height = input.height();
    std::vector<float> gray(static_cast<std::size_t>(width) * height);
    Image output(width, height);
    rows(height, pool, [&](int begin, int end) {
        for (int y = begin; y < end; ++y) {
            const auto* source = input.row(y);
            auto* target = gray.data() + static_cast<std::size_t>(y) * width;
            for (int x = 0; x < width; ++x)
                target[x] = 0.299f * source[x * 3] + 0.587f * source[x * 3 + 1] + 0.114f * source[x * 3 + 2];
        }
    });
    rows(height, pool, [&](int begin, int end) {
        for (int y = begin; y < end; ++y) {
            const auto* upper = gray.data() + static_cast<std::size_t>(std::max(y - 1, 0)) * width;
            const auto* center = gray.data() + static_cast<std::size_t>(y) * width;
            const auto* lower = gray.data() + static_cast<std::size_t>(std::min(y + 1, height - 1)) * width;
            auto* target = output.row(y);
            for (int x = 0; x < width; ++x) {
                const int left = std::max(x - 1, 0), right = std::min(x + 1, width - 1);
                const float gx = -upper[left] + upper[right] - 2 * center[left] + 2 * center[right]
                                 - lower[left] + lower[right];
                const float gy = -upper[left] - 2 * upper[x] - upper[right]
                                 + lower[left] + 2 * lower[x] + lower[right];
                // Raw Sobel magnitude, saturated to 8 bits (no normalization).
                const auto magnitude = byte(std::sqrt(gx * gx + gy * gy));
                target[x * 3] = target[x * 3 + 1] = target[x * 3 + 2] = magnitude;
            }
        }
    });
    return output;
}

Image resize_bilinear(const Image& input, int width, int height, ThreadPool* pool) {
    Image output(width, height);
    const auto horizontal = samples(input.width(), width);
    const auto vertical = samples(input.height(), height);
    rows(height, pool, [&](int begin, int end) {
        for (int y = begin; y < end; ++y) {
            const auto sy = vertical[static_cast<std::size_t>(y)];
            const auto* upper = input.row(sy.first);
            const auto* lower = input.row(sy.second);
            auto* target = output.row(y);
            for (int x = 0; x < width; ++x) {
                const auto sx = horizontal[static_cast<std::size_t>(x)];
                for (int c = 0; c < 3; ++c) {
                    const float a = upper[sx.first * 3 + c] * (1 - sx.weight) + upper[sx.second * 3 + c] * sx.weight;
                    const float b = lower[sx.first * 3 + c] * (1 - sx.weight) + lower[sx.second * 3 + c] * sx.weight;
                    target[x * 3 + c] = byte(a * (1 - sy.weight) + b * sy.weight);
                }
            }
        }
    });
    return output;
}

Image pipeline(const Image& input, int width, int height, int radius, double sigma, ThreadPool* pool) {
    auto blurred = gaussian_blur(input, radius, sigma, pool);
    auto edges = sobel_edges(blurred, pool);
    return resize_bilinear(edges, width, height, pool);
}

} // namespace imagefilter
