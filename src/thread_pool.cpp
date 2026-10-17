#include "imagefilter/thread_pool.hpp"

namespace imagefilter {

ThreadPool::ThreadPool(std::size_t workers) {
    if (workers == 0 || workers > 256)
        throw std::invalid_argument("thread count must be between 1 and 256");
    workers_.reserve(workers);
    try {
        for (std::size_t i = 0; i < workers; ++i) {
            workers_.emplace_back([this] {
                for (;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        ready_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
                        if (stopping_ && tasks_.empty()) return;
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    task(); // packaged_task captures exceptions for the caller
                }
            });
        }
    } catch (...) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        ready_.notify_all();
        for (auto& worker : workers_) worker.join();
        throw;
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    ready_.notify_all();
    for (auto& worker : workers_) worker.join();
}

std::future<void> ThreadPool::submit(std::function<void()> task) {
    auto packaged = std::make_shared<std::packaged_task<void()>>(std::move(task));
    auto future = packaged->get_future();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) throw std::runtime_error("cannot submit to a stopped pool");
        tasks_.emplace([packaged] { (*packaged)(); });
    }
    ready_.notify_one();
    return future;
}

} // namespace imagefilter
