#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace krkrvita {

// Yuri partitions software compositing into independent numbered jobs. Keep
// the worker threads alive between draws: creating Vita threads for every
// dirty rectangle costs more than many small KAG operations themselves.
//
// The engine thread always owns job zero. Worker threads handle jobs one and
// above and are marked so Yuri texture adapters can avoid worker-side changes
// to shared metadata while still writing their disjoint scanlines.
class RenderTaskPool {
public:
    using Task = std::function<void(int)>;
    using WorkerInitializer = std::function<void(int)>;

    explicit RenderTaskPool(int worker_count,
                            WorkerInitializer initialize_worker = {})
        : owner_(std::this_thread::get_id()),
          initialize_worker_(std::move(initialize_worker)) {
        if (worker_count < 0)
            throw std::invalid_argument("negative render worker count");
        workers_.reserve(static_cast<std::size_t>(worker_count));
        try {
            for (int index = 1; index <= worker_count; ++index)
                workers_.emplace_back([this, index] { worker_loop(index); });
        } catch (...) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stop_ = true;
                ++generation_;
            }
            work_condition_.notify_all();
            join_workers();
            throw;
        }

        std::unique_lock<std::mutex> lock(mutex_);
        ready_condition_.wait(lock, [this] {
            return ready_workers_ == workers_.size();
        });
        if (startup_exception_) {
            stop_ = true;
            ++generation_;
            work_condition_.notify_all();
            lock.unlock();
            join_workers();
            std::rethrow_exception(startup_exception_);
        }
    }

    ~RenderTaskPool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
            ++generation_;
        }
        work_condition_.notify_all();
        join_workers();
    }

    RenderTaskPool(const RenderTaskPool&) = delete;
    RenderTaskPool& operator=(const RenderTaskPool&) = delete;

    void run(int task_count, const Task& task) {
        if (task_count <= 0) return;
        if (task_count == 1 || workers_.empty() ||
            std::this_thread::get_id() != owner_ || dispatch_active_) {
            for (int index = 0; index < task_count; ++index) task(index);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            dispatch_active_ = true;
            current_task_ = &task;
            current_task_count_ = task_count;
            next_worker_task_.store(1, std::memory_order_relaxed);
            cancel_.store(false, std::memory_order_relaxed);
            completed_workers_ = 0;
            task_exception_ = nullptr;
            ++generation_;
        }
        work_condition_.notify_all();

        // Job zero remains on the engine thread. Besides using all three Vita
        // application cores, this preserves Yuri's main-thread-only updates to
        // texture opacity/cache metadata.
        execute_one(task, 0);

        std::unique_lock<std::mutex> lock(mutex_);
        completion_condition_.wait(lock, [this] {
            return completed_workers_ == workers_.size();
        });
        const std::exception_ptr failure = task_exception_;
        current_task_ = nullptr;
        current_task_count_ = 0;
        dispatch_active_ = false;
        lock.unlock();
        if (failure) std::rethrow_exception(failure);
    }

    static bool in_worker_context() { return worker_context_; }

private:
    void worker_loop(int worker_index) {
        try {
            if (initialize_worker_) initialize_worker_(worker_index);
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!startup_exception_)
                startup_exception_ = std::current_exception();
        }

        std::unique_lock<std::mutex> lock(mutex_);
        ++ready_workers_;
        ready_condition_.notify_one();
        std::size_t observed_generation = generation_;
        for (;;) {
            work_condition_.wait(lock, [this, observed_generation] {
                return stop_ || generation_ != observed_generation;
            });
            if (stop_) return;
            observed_generation = generation_;
            const Task* task = current_task_;
            const int task_count = current_task_count_;
            lock.unlock();

            worker_context_ = true;
            while (!cancel_.load(std::memory_order_relaxed)) {
                const int index =
                    next_worker_task_.fetch_add(1, std::memory_order_relaxed);
                if (index >= task_count) break;
                execute_one(*task, index);
            }
            worker_context_ = false;

            lock.lock();
            ++completed_workers_;
            if (completed_workers_ == workers_.size())
                completion_condition_.notify_one();
        }
    }

    void execute_one(const Task& task, int index) {
        if (cancel_.load(std::memory_order_relaxed)) return;
        try {
            task(index);
        } catch (...) {
            cancel_.store(true, std::memory_order_relaxed);
            std::lock_guard<std::mutex> lock(mutex_);
            if (!task_exception_)
                task_exception_ = std::current_exception();
        }
    }

    void join_workers() noexcept {
        for (std::thread& worker : workers_)
            if (worker.joinable()) worker.join();
    }

    const std::thread::id owner_;
    const WorkerInitializer initialize_worker_;
    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable ready_condition_;
    std::condition_variable work_condition_;
    std::condition_variable completion_condition_;
    std::size_t ready_workers_ = 0;
    std::size_t completed_workers_ = 0;
    std::size_t generation_ = 0;
    bool stop_ = false;
    bool dispatch_active_ = false;
    const Task* current_task_ = nullptr;
    int current_task_count_ = 0;
    std::atomic<int> next_worker_task_{1};
    std::atomic<bool> cancel_{false};
    std::exception_ptr startup_exception_;
    std::exception_ptr task_exception_;

    inline static thread_local bool worker_context_ = false;
};

} // namespace krkrvita
