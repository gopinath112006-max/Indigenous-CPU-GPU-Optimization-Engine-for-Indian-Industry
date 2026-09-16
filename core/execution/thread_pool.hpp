#pragma once

#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <future>
#include <atomic>
#include <memory>
#include <cstddef>
#include <stdexcept>

namespace hypernova::execution {

class ThreadPool {
public:
    explicit ThreadPool(std::size_t num_threads = 0);
    ~ThreadPool();

    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
        using return_type = decltype(f(args...));
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        std::future<return_type> res = task->get_future();

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (stop_) throw std::runtime_error("enqueue on stopped ThreadPool");
            tasks_.emplace([task]() { (*task)(); });
        }
        condition_.notify_one();
        return res;
    }

    std::size_t num_threads() const { return workers_.size(); }
    std::size_t pending_tasks() const {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        return tasks_.size();
    }

    std::exception_ptr failure() const { return failure_; }

    void wait_idle();

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_{false};
    std::atomic<std::size_t> active_tasks_{0};
    std::condition_variable idle_condition_;
    std::exception_ptr failure_;
};

class WorkStealingQueue {
public:
    WorkStealingQueue() = default;
    ~WorkStealingQueue() = default;

    WorkStealingQueue(const WorkStealingQueue&) = delete;
    WorkStealingQueue& operator=(const WorkStealingQueue&) = delete;
    WorkStealingQueue(WorkStealingQueue&&) = default;
    WorkStealingQueue& operator=(WorkStealingQueue&&) = default;

    template<typename T>
    bool try_push(T&& task) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::forward<T>(task));
        return true;
    }

    template<typename T>
    bool try_pop(T& task) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        task = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    std::queue<std::function<void()>> queue_;
    mutable std::mutex mutex_;
};

// Work-stealing pool with a blocking (condition-variable) worker loop so idle
// threads do not burn CPU while waiting for work, a robust shutdown path that
// wakes and joins every worker, and per-task exception containment so one
// failing task does not destroy a worker thread.
class WorkStealingThreadPool {
public:
    explicit WorkStealingThreadPool(std::size_t num_threads = 0);
    ~WorkStealingThreadPool();

    template<typename F>
    void submit(F&& task) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_) throw std::runtime_error("submit on stopped WorkStealingThreadPool");
            std::size_t idx = next_queue_++ % queues_.size();
            queues_[idx]->try_push(std::forward<F>(task));
        }
        cv_.notify_all();
    }

    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
        using return_type = decltype(f(args...));
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        std::future<return_type> res = task->get_future();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_) throw std::runtime_error("enqueue on stopped WorkStealingThreadPool");
            std::size_t idx = next_queue_++ % queues_.size();
            queues_[idx]->try_push([task]() { (*task)(); });
        }
        cv_.notify_all();
        return res;
    }

    std::size_t num_threads() const { return workers_.size(); }

    // Blocks until all submitted tasks have finished. Tasks enqueued after
    // this call are not covered (they wake the pool again).
    void wait_idle();

    // First uncaught worker exception, if any (order unspecified).
    std::exception_ptr failure() const { return failure_; }

private:
    std::vector<std::thread> workers_;
    std::vector<std::unique_ptr<WorkStealingQueue>> queues_;
    std::atomic<std::size_t> next_queue_{0};
    std::atomic<bool> stop_{false};
    std::atomic<std::size_t> busy_{0};
    std::mutex mutex_;
    std::condition_variable cv_;
    std::exception_ptr failure_;

    void worker_loop(std::size_t my_index);
    bool has_own_or_stealable(std::size_t my_index);
    bool is_empty_all();
    static bool has_task(const std::vector<std::unique_ptr<WorkStealingQueue>>& qs);
};

} // namespace hypernova::execution