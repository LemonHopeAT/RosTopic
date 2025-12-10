
#ifndef ARCH_CCR_EXECUTOR_LOCK_FREE_H
#define ARCH_CCR_EXECUTOR_LOCK_FREE_H

#include "IMessage.h"
#include "Topic.h"
#include "WaitSet.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace arch
{

    using std::chrono_literals::operator""ms;

    // Лёгкая lock-free версия Executor-а. Она избегает мьютекса для списка топиков
    // и использует односвязный список с CAS для вставки и ленивого удаления.
    // ВАЖНО: для упрощения управления памятью узлы списка освобождаются только
    // при stop() — это простой, но практичный способ избежать сложных схем
    // безопасного удаления (hazard pointers / epoch reclamation).

    class Executor
    {
    private:
        struct Node
        {
            std::weak_ptr<void> weak_topic;
            std::function<void()> process_func;
            std::atomic<Node*> next{nullptr};
            std::atomic<bool> removed{false};

            Node(std::weak_ptr<void> w, std::function<void()> f)
            : weak_topic(std::move(w)), process_func(std::move(f)) {}
        };

    public:
        Executor(size_t thread_count = 1)
        : running_(false), thread_count_(thread_count > 0 ? thread_count : 1), topics_processed_(0), head_(nullptr)
        {
        }

        ~Executor()
        {
            stop();
            // Гарантируем освобождение оставшихся узлов
            cleanup_all_nodes();
        }

        void start()
        {
            bool expected = false;
            if (!running_.compare_exchange_strong(expected, true))
                return;    // уже запущен

            // создаём потоки
            workers_.clear();
            for (size_t i = 0; i < thread_count_; ++i)
                workers_.emplace_back([this]() { worker_thread(); });
        }

        void stop()
        {
            bool expected = true;
            if (!running_.compare_exchange_strong(expected, false))
                return;    // уже остановлен

            // Разбудим все воркеры, чтобы они вышли
            wake_all();

            for (auto& w : workers_)
                if (w.joinable())
                    w.join();

            workers_.clear();

            // Только после того, как все потоки завершены — можно безопасно
            // уничтожить узлы списка и освобождать память.
            cleanup_all_nodes();
        }

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
                    int processed          = 0;
                    const int max_per_slot = 100;    // увеличить — меньше переключений
                    while (processed++ < max_per_slot)
                    {
                        auto msg = slot->pop_message();
                        if (!msg.has_value())
                            break;

                        try
                        {
                            slot->execute_callback(std::move(*msg));
                        }
                        catch (const std::exception&)
                        {
                        }
                        catch (...)
                        {
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
        void remove_topic(std::shared_ptr<void> topic)
        {
            // Проходим по всем узлам и помечаем те, чей weak.lock == topic
            Node* cur = head_.load(std::memory_order_acquire);
            while (cur)
            {
                if (!cur->removed.load(std::memory_order_acquire))
                {
                    auto ptr = cur->weak_topic.lock();
                    if (!ptr || ptr.get() == topic.get())
                    {
                        // пометим как удалённый
                        cur->removed.store(true, std::memory_order_release);
                    }
                }
                cur = cur->next.load(std::memory_order_acquire);
            }
        }

        void spin_once(std::chrono::milliseconds timeout = 1ms)
        {
            if (!running_.load(std::memory_order_acquire))
                return;

            Node* cur = head_.load(std::memory_order_acquire);
            while (cur)
            {
                // Если помечен — пропускаем
                bool removed = cur->removed.load(std::memory_order_acquire);
                if (!removed)
                {
                    try
                    {
                        if (cur->process_func)
                            cur->process_func();
                    }
                    catch (...)
                    {
                    }
                }
                cur = cur->next.load(std::memory_order_acquire);
            }

            waitset_.wait_for(timeout);
            topics_processed_++;

            // Замечание: мы не удаляем узлы тут (см. комментарии выше).
        }

        void spin_some()
        {
            for (int i = 0; i < 100; ++i)
                spin_once(std::chrono::milliseconds(1));
        }

        void spin_all(int iterations = 1000)
        {
            for (int i = 0; i < iterations; ++i)
                spin_once(std::chrono::milliseconds(1));
        }

        void wake_one()
        {
            waitset_.notify();
        }

        void wake_all()
        {
            for (size_t i = 0; i < thread_count_; ++i)
                waitset_.notify();
        }

        size_t get_topics_processed() const
        {
            return topics_processed_.load();
        }

    private:
        void worker_thread()
        {
            while (running_.load(std::memory_order_acquire))
            {
                try
                {
                    spin_once(100ms);
                }
                catch (...)
                {
                    arch::sleep_for(10ms);
                }
            }
        }

        // Очищает все узлы списка. Вызывать ТОЛЬКО когда все воркеры остановлены.
        void cleanup_all_nodes()
        {
            Node* cur = head_.exchange(nullptr, std::memory_order_acq_rel);
            while (cur)
            {
                Node* next = cur->next.load(std::memory_order_acquire);
                delete cur;
                cur = next;
            }
        }

        std::atomic<bool> running_;
        std::atomic<size_t> topics_processed_;
        size_t thread_count_;
        std::vector<std::thread> workers_;

        // Lock-free односвязный список: head указывает на первый Node*.
        std::atomic<Node*> head_;

        WaitSet waitset_;
    };

}    // namespace arch

#endif    // ARCH_CCR_EXECUTOR_LOCK_FREE_H
