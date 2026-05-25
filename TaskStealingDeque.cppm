export module TaskStealingDeque;

import <cstdint>;
import <cstddef>;
import <atomic>;
import <new>;
import <utility>;

export template <typename TaskType, std::size_t Capacity>
class TaskStealingDeque {
private:
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two.");
    static constexpr std::size_t Mask = Capacity - 1;

    alignas(64) TaskType storage_[Capacity];
    
    alignas(64) std::atomic<int64_t> head_{0};
    alignas(64) std::atomic<int64_t> tail_{0};

public:
    TaskStealingDeque() noexcept = default;
    ~TaskStealingDeque() = default;

    TaskStealingDeque(const TaskStealingDeque&) = delete;
    TaskStealingDeque& operator=(const TaskStealingDeque&) = delete;
    TaskStealingDeque(TaskStealingDeque&&) = delete;
    TaskStealingDeque& operator=(TaskStealingDeque&&) = delete;

    bool push(TaskType&& task) noexcept {
        int64_t h = head_.load(std::memory_order_relaxed);
        int64_t t = tail_.load(std::memory_order_acquire);

        if ((h - t) >= static_cast<int64_t>(Capacity)) {
            return false;
        }

        storage_[h & Mask] = std::move(task);
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

    bool pop(TaskType& task) noexcept {
        int64_t h = head_.load(std::memory_order_relaxed) - 1;
        head_.store(h, std::memory_order_seq_cst);
        int64_t t = tail_.load(std::memory_order_seq_cst);

        if (t <= h) {
            if (t == h) {
                if (!tail_.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_seq_cst)) {
                    head_.store(h + 1, std::memory_order_release);
                    return false;
                }
                task = std::move(storage_[h & Mask]);
                head_.store(h + 1, std::memory_order_release);
                return true;
            }
            
            task = std::move(storage_[h & Mask]);
            return true;
        }

        head_.store(h + 1, std::memory_order_release);
        return false;
    }

    bool steal(TaskType& task) noexcept {
    while (true) {
        int64_t t = tail_.load(std::memory_order_acquire);
        int64_t h = head_.load(std::memory_order_acquire);

        if (t >= h) {
            return false;
        }

        TaskType local_task = storage_[t & Mask]; 

        if (tail_.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed)) {
            task = std::move(local_task);
            return true;
        }
    }
}

};
