
// Copyright (C) 2026 mxreal64
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://gnu.org>.

export module TaskStealingDeque;

import std;

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
// ronaldoooooooooooo
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
