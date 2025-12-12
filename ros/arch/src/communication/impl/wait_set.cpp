/**
 * @file wait_set.cpp
 * @brief Wait set implementation
 * @date 15.12.2025
 * @version 1.0.0
 */

#include "include/communication/impl/wait_set.h"
#include <chrono>
#include <thread>

namespace arch::experimental
{

    void WaitSet::notify() noexcept
    {
        seq_.fetch_add(1, std::memory_order_acq_rel);
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    void WaitSet::notify_all() noexcept
    {
        seq_.fetch_add(1, std::memory_order_acq_rel);
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    void WaitSet::wait()
    {
        uint64_t cur = seq_.load(std::memory_order_acquire);

        int spin_count     = 0;
        const int max_spin = 1000;

        while (seq_.load(std::memory_order_acquire) == cur)
        {
            if (spin_count < max_spin)
            {
                ++spin_count;
#ifdef __x86_64__
                asm volatile("pause" ::
                                 : "memory");
#elif defined(__aarch64__)
                asm volatile("yield" ::
                                 : "memory");
#endif
            }
            else
            {
                std::this_thread::yield();
            }
        }
    }

    bool WaitSet::try_wait() noexcept
    {
        static thread_local uint64_t last_seen = 0;
        uint64_t current                       = seq_.load(std::memory_order_acquire);
        if (current != last_seen)
        {
            last_seen = current;
            return true;
        }
        return false;
    }

}    // namespace arch::experimental
