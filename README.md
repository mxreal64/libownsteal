# libownsteal 

A blazing-fast, single-file lock-free Chase-Lev Task-Stealing Deque implemented in standard **C++20/C++23 Modules**.

Designed for ultra-low latency scheduling systems, parallel game engines, and high-performance concurrent runtimes.

---

##  Key Features

* **Lock-Free Architecture:** Single-writer, multi-reader queues utilizing explicit atomic memory barriers (`std::memory_order_release` / `acquire`).
* **Hardware Cache Isolation:** Key indices are explicitly separated with `alignas(64)` to completely prevent CPU cache false sharing.
* **Modern C++ Modules:** Full native support for standard module interfaces via `export module TaskStealingDeque;`.
* **Zero Runtimes Allocations:** Built around a static ring-buffer pattern with fixed capacities to guarantee deterministic execution time.

---

##  Quick Start

Because **libownsteal** is built entirely using standard C++ modules, you can drop the `.cppm` file directly into your workspace and import it:

```cpp
import TaskStealingDeque;
#include <iostream>
#include <utility> 

struct MyTask {
    int id;
    // Define a default constructor so the receiver objects can be initialized
    MyTask() : id(0) {} 
    MyTask(int identifier) : id(identifier) {}
    
    void execute() const { std::cout << "Running task " << id << "\n"; }
};

int main() {
    // Capacity must be a power of two
    TaskStealingDeque<MyTask, 1024> queue;
    
    // 1. Push from the owner thread (explicitly moving the temporary object)
    bool pushed = queue.push(MyTask(42));
    if (pushed) {
        std::cout << "Task pushed successfully!\n";
    }
    
    // 2. Pop from the owner thread
    MyTask localTask;
    if (queue.pop(localTask)) {
        localTask.execute();
    }
    
    // 3. Concurrent thief threads can safely steal j*bs
    MyTask stolenTask;
    if (queue.steal(stolenTask)) {
        stolenTask.execute();
    }
    
    return 0;
}

```

---

##  Performance Context

* **Memory Layout:** Keeps memory footprints perfectly contiguous in cache.
* **Algorithmic Bounds:** Push and Pop operations execute in $O(1)$ constant time with minimal atomic contention.

---

##  License

This project is open-source software licensed under the **GNU General Public License v3 (GPL-3.0)**. See the `LICENSE` file for details.

---

copyright mxreal64, 2026
