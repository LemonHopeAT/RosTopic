
#ifndef ARCH_COMM_WAITSET_H
#define ARCH_COMM_WAITSET_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

class WaitSet
{
private:
    alignas(64) std::atomic<uint64_t> seq_{0};

public:
    void notify() noexcept
    {
        // Увеличиваем последовательность с полным барьером
        seq_.fetch_add(1, std::memory_order_acq_rel);
        // Используем fence для гарантии видимости изменений
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    void notify_all() noexcept
    {
        // Для notify_all в lock-free реализации делаем то же самое
        seq_.fetch_add(1, std::memory_order_acq_rel);
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    void wait()
    {
        uint64_t cur = seq_.load(std::memory_order_acquire);

        // Busy-wait с exponential backoff
        int spin_count     = 0;
        const int max_spin = 1000;

        while (seq_.load(std::memory_order_acquire) == cur)
        {
            if (spin_count < max_spin)
            {
                // Пробуем спин-лок
                ++spin_count;
// Пауза для процессора (помогает в гипертрединге)
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
                // После многих попыток - уступаем процессор
                std::this_thread::yield();
            }
        }
    }

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
                return false;    // Таймаут
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
                // Рассчитываем оставшееся время для sleep
                auto remaining = deadline - now;
                if (remaining > std::chrono::milliseconds(1))
                {
                    // Спим небольшими интервалами
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

    // Дополнительный метод для проверки без ожидания
    bool try_wait() noexcept
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
};

#endif    // ARCH_COMM_WAITSET_H
