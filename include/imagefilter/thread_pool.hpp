#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace imagefilter {

// Fixed workers; destruction drains queued work and joins every worker.
class ThreadPool {
public:
    explicit ThreadPool(std::size_t workers);
    ~ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    std::size_t size() const noexcept { return workers_.size(); }
    std::future<void> submit(std::function<void()> task);

    // Caller must be outside this pool. Every submitted task is joined even if
    // another fails; captured row buffers remain alive until the barrier ends.
    template<class Function>
    void parallel_rows(int rows, Function&& function, int grain = 16) {
        if (rows < 0 || grain <= 0) throw std::invalid_argument("invalid row range or grain");
        if (rows == 0) return;
        const auto task_count = std::min(size(), static_cast<std::size_t>((rows - 1) / grain + 1));
        std::atomic<int> next{0};
        std::vector<std::future<void>> pending;
        pending.reserve(task_count);
        std::exception_ptr error;
        try {
            for (std::size_t i = 0; i < task_count; ++i) {
                pending.push_back(submit([&] {
                    for (;;) {
                        const int begin = next.fetch_add(grain, std::memory_order_relaxed);
                        if (begin >= rows) return;
                        function(begin, std::min(begin + grain, rows));
                    }
                }));
            }
        } catch (...) { error = std::current_exception(); }
        for (auto& future : pending) {
            try { future.get(); }
            catch (...) { if (!error) error = std::current_exception(); }
        }
        if (error) std::rethrow_exception(error);
    }
private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable ready_;
    bool stopping_ = false;
};

} // namespace imagefilter
