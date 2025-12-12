/**
 * @file executor.h
 * @brief Lock-free executor for processing messages from topics
 * @date 15.12.2025
 * @version 1.0.0
 * @ingroup arch_experimental
 */

#ifndef ARCH_CCR_EXECUTOR_LOCK_FREE_H
#define ARCH_CCR_EXECUTOR_LOCK_FREE_H

#include "concurrency/impl/queue_thread_pool.h"
#include "topic.h"
#include "wait_set.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace arch::experimental
{

    using std::chrono_literals::operator""ms;

    /**
     * @brief Lock-free executor for processing messages from topics
     * @ingroup arch_experimental
     *
     * Лёгкая lock-free версия Executor-а. Она избегает мьютекса для списка топиков
     * и использует односвязный список с CAS для вставки и ленивого удаления.
     * ВАЖНО: для упрощения управления памятью узлы списка освобождаются только
     * при stop() — это простой, но практичный способ избежать сложных схем
     * безопасного удаления (hazard pointers / epoch reclamation).
     */
    class Executor
    {
    private:
        /**
         * @brief Topic node in lock-free linked list of topics
         */
        struct TopicNode
        {
            std::shared_ptr<void> shared_topic;     ///< Shared pointer to topic (for performance)
            std::function<void()> process_func;    ///< Function to process topic messages
            std::atomic<TopicNode*> next{nullptr};      ///< Next node in list
            std::atomic<bool> removed{false};      ///< Removal flag

            TopicNode(std::shared_ptr<void> s, std::function<void()> f)
            : shared_topic(std::move(s)), process_func(std::move(f)) {}
        };

    public:
        /**
         * @brief Constructs executor
         * @param thread_count Number of worker threads (default: 1)
         */
        Executor(size_t thread_count = 1);

        ~Executor();

        void start();

        void stop();

        // Добавление топика lock-free: просто вставка узла в голову списка
        template <typename Type>
        void add_topic(std::shared_ptr<Topic<Type>> topic)
        {
            // Store shared_ptr directly to avoid weak_ptr.lock() overhead
            // TopicNode will be cleaned up only on stop(), so this is safe
            auto shared_topic = std::shared_ptr<void>(std::static_pointer_cast<void>(topic));

            auto process = [shared_topic]() {
                auto topic_ptr = std::static_pointer_cast<Topic<Type>>(
                    std::static_pointer_cast<void>(shared_topic));
                if (!topic_ptr)
                    return;

                // Get slots once - avoid multiple calls
                auto slots = topic_ptr->get_slots();
                if (slots.empty())
                    return;

                // Process messages with balanced batch size for low latency
                // Smaller batches reduce latency but maintain throughput
                for (auto slot : slots)
                {
                    if (!slot)
                        continue;
                    
                    int processed          = 0;
                    // Adaptive batch size: process more messages if queue is large
                    // Increased batch size for better throughput and reduced Duration
                    size_t queue_size = slot->queue_size();
                    int max_per_slot = queue_size > 5000 ? 5000 : (queue_size > 1000 ? 2000 : 500);  // Increased batch sizes
                    while (processed < max_per_slot)
                    {
                        auto msg = slot->pop_message();
                        if (!msg.has_value())
                            break;    // Queue is empty, move to next slot

                        try
                        {
                            slot->execute_callback(std::move(*msg));
                            processed++;
                        }
                        catch (const std::exception&)
                        {
                            processed++;    // Count even if callback throws
                        }
                        catch (...)
                        {
                            processed++;    // Count even if callback throws
                        }
                    }
                }
            };

            TopicNode* new_node = new TopicNode(std::move(shared_topic), std::move(process));

            // register wake callback in topic so publish() will wake executor
            topic->set_wake_callback([this]() { this->wake_one(); });

            // lock-free push front
            TopicNode* old_head = head_.load(std::memory_order_acquire);
            do
            {
                new_node->next.store(old_head, std::memory_order_relaxed);
            } while (!head_.compare_exchange_weak(old_head, new_node,
                                                  std::memory_order_release,
                                                  std::memory_order_acquire));
        }

        // Удаление топика: помечаем узел как удалённый. Фактическая очистка — при stop().
        void remove_topic(std::shared_ptr<void> topic);

        void spin_once(std::chrono::milliseconds timeout = 1ms);

        void spin_some();

        void spin_all(int iterations = 1000);

        void wake_one();

        void wake_all();

        size_t get_topics_processed() const;

        /**
         * @brief Cleanup all topic nodes in list
         * @note Вызывать ТОЛЬКО когда все воркеры остановлены
         */
        void cleanup_all_nodes();

        std::atomic<bool> running_;
        std::atomic<size_t> topics_processed_;
        size_t thread_count_;
        std::unique_ptr<arch::impl::QueueThreadPool> thread_pool_;    ///< Thread pool for worker threads

        // Lock-free односвязный список: head указывает на первый TopicNode*.
        std::atomic<TopicNode*> head_;

        WaitSet waitset_;
    };

}    // namespace arch::experimental

#endif    // !ARCH_CCR_EXECUTOR_LOCK_FREE_H
