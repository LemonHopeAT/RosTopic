/**
 * @file executor.h
 * @brief Lock-free executor for processing messages from topics
 * @date 2025
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
         * @brief Node in lock-free linked list of topics
         */
        struct Node
        {
            std::weak_ptr<void> weak_topic;        ///< Weak pointer to topic
            std::function<void()> process_func;    ///< Function to process topic messages
            std::atomic<Node*> next{nullptr};      ///< Next node in list
            std::atomic<bool> removed{false};      ///< Removal flag

            Node(std::weak_ptr<void> w, std::function<void()> f)
            : weak_topic(std::move(w)), process_func(std::move(f)) {}
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
        template <typename MessageT>
        void add_topic(std::shared_ptr<Topic<MessageT>> topic)
        {
            auto weak_topic = std::weak_ptr<Topic<MessageT>>(topic);

            auto process = [weak_topic]() {
                auto topic_ptr = weak_topic.lock();
                if (!topic_ptr)
                    return;

                auto slots = topic_ptr->get_slots();
                for (auto& slot : slots)
                {
                    // Process all available messages until queue is empty
                    // This ensures we drain the queue efficiently under high load
                    // Use a reasonable limit to prevent starvation of other topics
                    int processed          = 0;
                    const int max_per_slot = 10000;    // Increased significantly for high-throughput scenarios
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

            Node* new_node = new Node(std::weak_ptr<void>(weak_topic), std::move(process));

            // register wake callback in topic so publish() will wake executor
            topic->set_wake_callback([this]() { this->wake_one(); });

            // lock-free push front
            Node* old_head = head_.load(std::memory_order_acquire);
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
         * @brief Cleanup all nodes in list
         * @note Вызывать ТОЛЬКО когда все воркеры остановлены
         */
        void cleanup_all_nodes();

        std::atomic<bool> running_;
        std::atomic<size_t> topics_processed_;
        size_t thread_count_;
        std::unique_ptr<arch::impl::QueueThreadPool> thread_pool_;    ///< Thread pool for worker threads

        // Lock-free односвязный список: head указывает на первый Node*.
        std::atomic<Node*> head_;

        WaitSet waitset_;
    };

}    // namespace arch::experimental

#endif    // !ARCH_CCR_EXECUTOR_LOCK_FREE_H
