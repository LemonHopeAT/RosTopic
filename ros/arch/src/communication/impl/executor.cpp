/**
 * @file executor.cpp
 * @brief Lock-free executor implementation
 * @date 15.12.2025
 * @version 1.0.0
 */

#include "include/communication/impl/executor.h"
#include <chrono>

#include "arch/utils.h"
namespace arch::experimental
{

    using std::chrono_literals::operator""ms;

    Executor::Executor(size_t thread_count)
    : running_(false),
      thread_count_(thread_count > 0 ? thread_count : 1),
      topics_processed_(0),
      head_(nullptr),
      thread_pool_(std::make_unique<arch::impl::QueueThreadPool>(thread_count > 0 ? thread_count : 1))
    {
    }

    Executor::~Executor()
    {
        stop();
        cleanup_all_nodes();
    }

    void Executor::start()
    {
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true))
            return;

        thread_pool_->start();

        for (size_t i = 0; i < thread_count_; ++i)
        {
            thread_pool_->pushTask([this]() {
                while (running_.load(std::memory_order_acquire))
                {
                    try
                    {
                        // Reduced timeout for lower latency - spin more frequently
                        spin_once(1ms);
                    }
                    catch (...)
                    {
                        arch::sleep_for(1ms);
                    }
                }
            });
        }
    }

    void Executor::stop()
    {
        bool expected = true;
        if (!running_.compare_exchange_strong(expected, false))
            return;

        wake_all();
        thread_pool_->pause();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        cleanup_all_nodes();
    }

    void Executor::remove_topic(std::shared_ptr<void> topic)
    {
        TopicNode* cur = head_.load(std::memory_order_acquire);
        while (cur)
        {
            if (!cur->removed.load(std::memory_order_acquire))
            {
                // Direct comparison - no need for weak_ptr.lock()
                if (cur->shared_topic.get() == topic.get())
                {
                    cur->removed.store(true, std::memory_order_release);
                }
            }
            cur = cur->next.load(std::memory_order_acquire);
        }
    }

    void Executor::spin_once(std::chrono::milliseconds timeout)
    {
        if (!running_.load(std::memory_order_acquire))
            return;

        bool had_work = false;
        TopicNode* cur = head_.load(std::memory_order_acquire);
        while (cur)
        {
            bool removed = cur->removed.load(std::memory_order_acquire);
            if (!removed)
            {
                try
                {
                    if (cur->process_func)
                    {
                        cur->process_func();
                        had_work = true;
                    }
                }
                catch (...)
                {
                }
            }
            cur = cur->next.load(std::memory_order_acquire);
        }

        // Only wait if we didn't process any work - reduces latency
        // Use shorter timeout for faster response
        if (!had_work && timeout.count() > 0)
        {
            waitset_.wait_for(timeout);
        }
        topics_processed_++;
    }

    void Executor::spin_some()
    {
        // Process multiple iterations without waiting for better throughput
        for (int i = 0; i < 200; ++i)  // Increased iterations
        {
            spin_once(std::chrono::milliseconds(0));  // No wait - process immediately
        }
    }

    void Executor::spin_all(int iterations)
    {
        for (int i = 0; i < iterations; ++i)
            spin_once(std::chrono::milliseconds(1));
    }

    void Executor::wake_one()
    {
        waitset_.notify();
    }

    void Executor::wake_all()
    {
        for (size_t i = 0; i < thread_count_; ++i)
            waitset_.notify();
    }

    size_t Executor::get_topics_processed() const
    {
        return topics_processed_.load();
    }

    void Executor::cleanup_all_nodes()
    {
        TopicNode* cur = head_.exchange(nullptr, std::memory_order_acq_rel);
        while (cur)
        {
            TopicNode* next = cur->next.load(std::memory_order_acquire);
            delete cur;
            cur = next;
        }
    }

}    // namespace arch::experimental
