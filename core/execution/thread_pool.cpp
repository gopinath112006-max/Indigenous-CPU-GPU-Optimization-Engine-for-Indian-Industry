#include "thread_pool.hpp"
#include <thread>

namespace hypernova::execution {

ThreadPool::ThreadPool(std::size_t num_threads) {
    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) num_threads = 4;
    }

    workers_.reserve(num_threads);
    for (std::size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(queue_mutex_);
                    condition_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                    if (stop_ && tasks_.empty()) return;
                    task = std::move(tasks_.front());
                    tasks_.pop();
                    active_tasks_++;
                }
                try {
                    task();
                } catch (...) {
                    std::exception_ptr e = std::current_exception();
                    {
                        std::lock_guard<std::mutex> lock(queue_mutex_);
                        if (!failure_) failure_ = e;
                    }
                }
                active_tasks_--;
                idle_condition_.notify_all();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        stop_ = true;
    }
    condition_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
}

void ThreadPool::wait_idle() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    idle_condition_.wait(lock, [this] { return tasks_.empty() && active_tasks_ == 0; });
}

WorkStealingThreadPool::WorkStealingThreadPool(std::size_t num_threads) {
    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) num_threads = 4;
    }

    queues_.reserve(num_threads);
    for (std::size_t i = 0; i < num_threads; ++i) {
        queues_.emplace_back(std::make_unique<WorkStealingQueue>());
    }
    workers_.reserve(num_threads);
    for (std::size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this, i] { worker_loop(i); });
    }
}

WorkStealingThreadPool::~WorkStealingThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
}

void WorkStealingThreadPool::wait_idle() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] {
        if (stop_) return true;
        return busy_ == 0 && !has_task(queues_);
    });
}

void WorkStealingThreadPool::worker_loop(std::size_t my_index) {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this, my_index] {
                if (stop_) return true;
                return has_own_or_stealable(my_index);
            });
            if (stop_ && is_empty_all()) return;

            // Own queue first (LIFO-steering could be added here; the shared
            // B&B warm cache already provides locality at a higher layer).
            if (!queues_[my_index]->try_pop(task)) {
                bool stolen = false;
                for (std::size_t i = 0; i < queues_.size(); ++i) {
                    if (i == my_index) continue;
                    if (queues_[i]->try_pop(task)) {
                        stolen = true;
                        break;
                    }
                }
                if (!stolen) continue; // woken spuriously; re-check predicate
            }
            busy_++;
        }

        try {
            task();
        } catch (...) {
            std::exception_ptr e = std::current_exception();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!failure_) failure_ = e;
            }
        }

        busy_--;
        cv_.notify_all();
    }
}

bool WorkStealingThreadPool::has_own_or_stealable(std::size_t my_index) {
    if (!queues_[my_index]->empty()) return true;
    for (std::size_t i = 0; i < queues_.size(); ++i) {
        if (i != my_index && !queues_[i]->empty()) return true;
    }
    return false;
}

bool WorkStealingThreadPool::is_empty_all() {
    for (const auto& q : queues_) {
        if (!q->empty()) return false;
    }
    return true;
}

bool WorkStealingThreadPool::has_task(const std::vector<std::unique_ptr<WorkStealingQueue>>& qs) {
    for (const auto& q : qs) {
        if (!q->empty()) return true;
    }
    return false;
}

} // namespace hypernova::execution