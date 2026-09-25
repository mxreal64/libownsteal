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
//
// -----------------------------------------------------------------------
// changes vs. the last iteration:
//
// 1. BUG (weak-memory correctness, silent on x86): steal() was missing the
//    seq_cst fence between its tail_/head_ loads that pop() already has
//    (via seq_cst store + seq_cst load) between its head_/tail_ ops. this
//    fence pair is what gives pop() and steal() a single consistent total
//    order over who wins the last remaining element (per that random paper
//    from 2013). plain acquire loads on both sides happen to work on
//    x86 because TSO forbids the reordering this fence rules out, but nothing
//    stops the two loads from reordering on arm/power, which can let a
//    concurrent pop() and steal() both believe they've won the same last
//    task, so i added the matching fence.
//
// 2. optimization: pop()'s head_.store(seq_cst) + tail_.load(seq_cst) is
//    replaced with the equivalent (and how the random paper from 2013 writes
//    it) relaxed store + one explicit seq_cst fence + relaxed load.
//    identical codegen on x86 (a seq_cst store already forces the fence a
//    seq_cst load doesn't need), but on weaker memory models this avoids a
//    compiler emitting a barrier around the load that the fence already
//    provides for both operations.
//
// 3. optimization: CAS failure orderings downgraded from seq_cst to
//    relaxed in both pop() and steal() (steal() already had this right).
//    on failure the exchanged-back value isn't used for anything but a
//    retry, so there's nothing for a stronger failure ordering to protect.
//
// 4. [[likely]]/[[unlikely]] on the hot/cold branchescuz a healthy
//    work-stealing scheduler spends almost all its time in push() succeeding
//    and pop()'s uncontested t < h path; the capacity-exceeded, contested-
//    last-slot, and empty-deque paths are the exceptions.
//    
//    NOTE: as for verification on arm/powerpc, i'm not doin that cuz who
//    is running an hft firm on a mac?
// -----------------------------------------------------------------------

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

    // Owner thread only.
    bool push(TaskType&& task) noexcept {
        int64_t h = head_.load(std::memory_order_relaxed);
        int64_t t = tail_.load(std::memory_order_acquire);

        if ((h - t) >= static_cast<int64_t>(Capacity)) [[unlikely]] {
            return false;
        }

        storage_[h & Mask] = std::move(task);
        head_.store(h + 1, std::memory_order_release);
        return true;
    }

    // Owner thread only.
    bool pop(TaskType& task) noexcept {
        int64_t h = head_.load(std::memory_order_relaxed) - 1;
        head_.store(h, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        int64_t t = tail_.load(std::memory_order_relaxed);

        if (t <= h) [[likely]] {
            if (t == h) [[unlikely]] {
                // Contested last slot: a thief may be concurrently reading
                // storage_[h & Mask] (see steal()'s unconditional read
                // before its CAS). Take our own copy first, before racing
                // on tail_, so we never move-assign into (or out of) a slot
                // a thief might still be copy-constructing from.
                TaskType local_task = storage_[h & Mask];

                if (!tail_.compare_exchange_strong(t, t + 1,
                        std::memory_order_seq_cst, std::memory_order_relaxed)) [[unlikely]] {
                    // Lost the race to a thief: they own this slot now.
                    head_.store(h + 1, std::memory_order_release);
                    return false;
                }

                task = std::move(local_task);
                head_.store(h + 1, std::memory_order_release);
                return true;
            }

            // t < h: uncontested, only the owner thread ever touches this slot.
            task = std::move(storage_[h & Mask]);
            return true;
        }

        // Deque was empty (t > h); restore head_ to a consistent empty state.
        head_.store(h + 1, std::memory_order_release);
        return false;
    }

    // Any thief thread.
    bool steal(TaskType& task) noexcept {
        while (true) {
            int64_t t = tail_.load(std::memory_order_acquire);
            std::atomic_thread_fence(std::memory_order_seq_cst);
            int64_t h = head_.load(std::memory_order_acquire);

            if (t >= h) [[unlikely]] {
                return false;
            }

            TaskType local_task = storage_[t & Mask];

            if (tail_.compare_exchange_strong(t, t + 1,
                    std::memory_order_seq_cst, std::memory_order_relaxed)) [[likely]] {
                task = std::move(local_task);
                return true;
            }
            // Lost the race (to another thief, or the owner's pop()).
            // t/h are now stale -- loop back and re-read both.
        }
    }
};
