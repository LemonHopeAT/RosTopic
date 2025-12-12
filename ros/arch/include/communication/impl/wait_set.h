/**
 * @file wait_set.h
 * @brief Lock-free wait set for thread synchronization
 * @date 15.12.2025
 * @version 1.0.0
 * @ingroup arch_experimental
 */

#ifndef ARCH_COMM_WAITSET_H
#define ARCH_COMM_WAITSET_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace arch::experimental
{

    /**
     * @brief Lock-free wait set for thread synchronization
     * @ingroup arch_experimental
     *
     * Provides lock-free notification mechanism using atomic sequence counter.
     * Threads can wait for notifications without blocking on mutexes.
     */
    class WaitSet
    {
    private:
        alignas(64) std::atomic<uint64_t> seq_{0};    ///< Sequence counter for notifications

    public:
        /**
         * @brief Notify one waiting thread
         */
        void notify() noexcept;

        /**
         * @brief Notify all waiting threads
         */
        void notify_all() noexcept;

        /**
         * @brief Wait for notification (blocking)
         */
        void wait();

        /**
         * @brief Wait for notification with timeout
         * @tparam Rep Time representation type
         * @tparam Period Time period type
         * @param timeout Timeout duration
         * @return true if notified, false if timeout
         */
        template <typename Rep, typename Period>
        bool wait_for(const std::chrono::duration<Rep, Period>& timeout)
        {
            auto start    = std::chrono::steady_clock::now();
            auto deadline = start + timeout;
            uint64_t cur  = seq_.load(std::memory_order_acquire);

            int spin_count     = 0;
            const int max_spin = 1000;

            while (seq_.load(std::memory_order_acquire) == cur)
            {
                auto now = std::chrono::steady_clock::now();
                if (now >= deadline)
                {
                    return false;
                }

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
                    auto remaining = deadline - now;
                    if (remaining > std::chrono::milliseconds(1))
                    {
                        std::this_thread::sleep_for(std::chrono::microseconds(100));
                    }
                    else
                    {
                        std::this_thread::yield();
                    }
                }
            }

            return true;
        }

        /**
         * @brief Try to wait without blocking
         * @return true if notification occurred, false otherwise
         */
        bool try_wait() noexcept;
    };

}    // namespace arch::experimental

#endif    // !ARCH_COMM_WAITSET_H
