#pragma once

#include "imagefilter/image.hpp"
#include "imagefilter/thread_pool.hpp"

namespace imagefilter {

// A null pool executes the exact same kernels on the calling thread.
Image gaussian_blur(const Image& input, int radius = 5, double sigma = 2.0,
                    ThreadPool* pool = nullptr);
Image sobel_edges(const Image& input, ThreadPool* pool = nullptr);
Image resize_bilinear(const Image& input, int width, int height, ThreadPool* pool = nullptr);
Image pipeline(const Image& input, int width, int height, int radius = 5,
               double sigma = 2.0, ThreadPool* pool = nullptr);

} // namespace imagefilter
