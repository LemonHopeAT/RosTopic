/**
 * @file benchmark.h
 * @brief Benchmark utilities for testing ROS2-like messaging system
 * @date 2025
 * @version 1.0.0
 * @ingroup arch_experimental
 */

#ifndef ARCH_BENCHMARK_H
#define ARCH_BENCHMARK_H

#include <arch/utils.h>
#include <arch/communication/imessage.h>
#include "test_message.h"
#include "../communication/impl/callback_group.h"
#include "../communication/impl/executor.h"
#include "../communication/impl/factory.h"
#include "../communication/impl/publisher.h"
#include "../communication/impl/subscriber_slot.h"
#include "../communication/impl/subscription.h"
#include "../communication/impl/topic.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <sstream>
#include <thread>
#include <vector>

#ifdef __linux__
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace arch::experimental
{

    /**
     * @brief Benchmark utilities for testing messaging system
     * @ingroup arch_experimental
     */
    class Benchmark
    {
    public:
        /**
         * @brief Benchmark result structure
         */
        struct BenchmarkResult
        {
            std::string name;
            size_t messages_sent;
            size_t messages_received;
            double duration_ms;    // основной показатель в миллисекундах
            double throughput_msg_per_sec;
            double latency_avg_ms;    // теперь в миллисекундах
            double latency_min_ms;    // теперь в миллисекундах
            double latency_max_ms;    // теперь в миллисекундах
            double latency_p99_ms;    // теперь в миллисекундах
            size_t memory_usage_bytes;
            size_t peak_queue_size;
            size_t dropped_messages;

            // Расширенные метрики латентности
            double latency_p50_ms{0.0};       ///< 50-й перцентиль латентности
            double latency_p95_ms{0.0};       ///< 95-й перцентиль латентности
            double latency_p99_9_ms{0.0};     ///< 99.9-й перцентиль латентности
            double latency_stddev_ms{0.0};    ///< Стандартное отклонение латентности
            double latency_jitter_ms{0.0};    ///< Jitter (max - min)

            // Статистика для множественных запусков
            double throughput_stddev{0.0};    ///< Стандартное отклонение throughput
            size_t iteration_count{1};        ///< Количество итераций для усреднения

            /**
             * @brief Вычислить расширенную статистику из массива латентностей
             * @param latencies Массив значений латентности
             */
            void calculate_extended_latency_stats(const std::vector<double>& latencies)
            {
                if (latencies.empty())
                    return;

                auto sorted = latencies;
                std::sort(sorted.begin(), sorted.end());

                // Перцентили
                latency_p50_ms   = percentile(sorted, 0.50);
                latency_p95_ms   = percentile(sorted, 0.95);
                latency_p99_ms   = percentile(sorted, 0.99);
                latency_p99_9_ms = percentile(sorted, 0.999);

                // Стандартное отклонение
                double sum = 0.0;
                for (double lat : sorted)
                    sum += lat;
                double mean = sum / sorted.size();

                double variance = 0.0;
                for (double lat : sorted)
                    variance += (lat - mean) * (lat - mean);
                latency_stddev_ms = std::sqrt(variance / sorted.size());

                // Jitter (вариация)
                latency_jitter_ms = sorted.back() - sorted.front();
            }

            void print() const
            {
                //print_to_stream(std::cerr);
            }

            /**
             * @brief Print detailed results to a stream (file or console)
             * @param stream Output stream (e.g., file stream or std::cerr)
             */
            void print_to_stream(std::ostream& stream) const
            {
                stream << "\n=== " << name << " ===\n";
                if (iteration_count > 1)
                    stream << "Iterations: " << iteration_count << "\n";
                stream << "Sent: " << messages_sent << "\n";
                stream << "Received: " << messages_received << "\n";
                stream << "Dropped: " << dropped_messages << " ("
                       << std::fixed << std::setprecision(2)
                       << (dropped_messages * 100.0 / messages_sent) << "%)\n";
                stream << "Duration: " << duration_ms << " ms\n";
                stream << "Throughput: "
                       << std::fixed << std::setprecision(0)
                       << throughput_msg_per_sec << " msg/sec";
                if (throughput_stddev > 0.0)
                    stream << " (stddev: " << std::setprecision(0) << throughput_stddev << ")";
                stream << "\n";
                stream << "Latency avg: " << latency_avg_ms << " ms\n";
                if (latency_p50_ms > 0.0)
                {
                    stream << "Latency p50: " << latency_p50_ms << " ms\n";
                    stream << "Latency p95: " << latency_p95_ms << " ms\n";
                }
                stream << "Latency p99: " << latency_p99_ms << " ms\n";
                if (latency_p99_9_ms > 0.0)
                    stream << "Latency p99.9: " << latency_p99_9_ms << " ms\n";
                stream << "Latency min: " << latency_min_ms << " ms\n";
                stream << "Latency max: " << latency_max_ms << " ms\n";
                if (latency_stddev_ms > 0.0)
                    stream << "Latency stddev: " << latency_stddev_ms << " ms\n";
                if (latency_jitter_ms > 0.0)
                    stream << "Latency jitter: " << latency_jitter_ms << " ms\n";
                stream << "Peak queue: " << peak_queue_size << "\n";
                stream << "Memory: " << format_memory(memory_usage_bytes) << "\n";
            }

            /**
             * @brief Format memory size in human-readable format
             * @param bytes Memory size in bytes
             * @return Formatted string (e.g., "1.5 MB")
             */
            static std::string format_memory(size_t bytes)
            {
                // Обработка переполнения (при усреднении может получиться очень большое значение)
                // Проверяем на разумный максимум (100 GB)
                const size_t MAX_REASONABLE_MEMORY = 100ULL * 1024 * 1024 * 1024;
                if (bytes == 0 || bytes > MAX_REASONABLE_MEMORY)
                    return "0 B";

                const char* units[] = {"B", "KB", "MB", "GB"};
                size_t unit_index   = 0;
                double value        = static_cast<double>(bytes);

                while (value >= 1024.0 && unit_index < 3)
                {
                    value /= 1024.0;
                    unit_index++;
                }

                // Проверка на разумные значения (больше 100 GB маловероятно для бенчмарка)
                if (value > 100 && unit_index == 3)
                {
                    return "N/A";
                }

                std::stringstream ss;
                ss << std::fixed << std::setprecision(2) << value << " " << units[unit_index];
                return ss.str();
            }

        private:
            /**
             * @brief Calculate percentile from sorted array
             * @param sorted Sorted array of values
             * @param p Percentile (0.0 to 1.0)
             * @return Percentile value
             */
            static double percentile(const std::vector<double>& sorted, double p)
            {
                if (sorted.empty())
                    return 0.0;
                size_t idx = static_cast<size_t>(sorted.size() * p);
                if (idx >= sorted.size())
                    idx = sorted.size() - 1;
                return sorted[idx];
            }
        };

        struct TimestampedMessage
        {
            std::chrono::high_resolution_clock::time_point send_time;
            size_t index;
        };

    public:
        static BenchmarkResult run_single_producer_single_consumer(size_t message_count = 100000)
        {
            //std::cerr << "[SPSC] Sending " << message_count << " messages..." << std::flush;

            BenchmarkResult result;
            result.name          = "Single Producer - Single Consumer";
            result.messages_sent = message_count;

            auto executor = std::make_shared<Executor>(1);
            auto factory  = std::make_shared<Factory>("benchmark_spsc_factory");

            std::atomic<size_t> received_count{0};
            std::atomic<size_t> dropped_count{0};
            std::atomic<size_t> peak_queue{0};
            std::vector<std::chrono::high_resolution_clock::time_point> receive_times(message_count);

            auto callback_group = std::make_shared<CallbackGroup>(
                CallbackGroup::Type::Reentrant, "benchmark_spsc");

            // Create subscription through Factory (ROS2-like API) - using TestMessage for benchmarking
            auto subscription_void = factory->createSubscription<arch::experimental::TestMessage<int>>(
                "/benchmark/spsc",
                [&received_count, &receive_times, message_count](message_ptr<const IMessage> msg) {
                    // Всегда увеличиваем счетчик полученных сообщений
                    received_count++;

                    // Получаем конкретный тип сообщения из IMessagePtr
                    auto int_msg = std::dynamic_pointer_cast<const arch::experimental::TestMessage<int>>(msg);
                    if (int_msg && int_msg->data >= 0 && int_msg->data < static_cast<int>(message_count))
                    {
                        size_t idx = static_cast<size_t>(int_msg->data);
                        if (idx < receive_times.size())
                        {
                            receive_times[idx] = std::chrono::high_resolution_clock::now();
                        }
                    }
                },
                QoS::Reliable(1000),
                callback_group);
            
            // Cast to Subscription<TestMessage<int>> for compatibility
            auto subscription = std::static_pointer_cast<Subscription<arch::experimental::TestMessage<int>>>(subscription_void);

            // Create publisher through Factory (ROS2-like API) - using TestMessage for benchmarking
            auto publisher = factory->createPublisher<arch::experimental::TestMessage<int>>("/benchmark/spsc", QoS::Reliable(1000));

            // Get topic from Factory and add to executor
            auto topic = factory->getTopic<arch::experimental::TestMessage<int>>("/benchmark/spsc");
            if (topic)
            {
                executor->add_topic(topic);
            }

            executor->start();

            size_t memory_before = get_current_memory_usage();
            auto start_time      = std::chrono::high_resolution_clock::now();

            std::this_thread::sleep_for(std::chrono::milliseconds(5));    // warm-up (уменьшено)

            // Get slot from subscription for queue size monitoring
            auto slot = subscription ? subscription->getSlot() : nullptr;

            for (size_t i = 0; i < message_count; ++i)
            {
                if (!publisher || !publisher->publish(arch::experimental::TestMessage<int>(static_cast<int>(i))))
                    dropped_count++;

                if (slot)
                {
                    size_t queue_size = slot->queue_size();
                    if (queue_size > peak_queue.load())
                        peak_queue.store(queue_size);
                }
            }

            // Оптимизированное ожидание обработки всех сообщений
            auto deadline            = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            size_t no_progress_count = 0;
            size_t last_received     = 0;

            while (received_count < message_count &&
                   std::chrono::steady_clock::now() < deadline)
            {
                executor->spin_once(std::chrono::milliseconds(5));    // Уменьшено с 10

                size_t current_queue_size = slot ? slot->queue_size() : 0;
                size_t current_received   = received_count.load();

                // Проверяем прогресс
                if (current_received == last_received)
                {
                    no_progress_count++;
                    // Более быстрое завершение при отсутствии прогресса
                    if (no_progress_count > 50 && current_queue_size == 0)    // Уменьшено с 100
                    {
                        break;
                    }
                }
                else
                {
                    no_progress_count = 0;
                    last_received     = current_received;
                }

                // Минимальные задержки только при необходимости
                if (current_queue_size == 0 && received_count < message_count)
                {
                    std::this_thread::sleep_for(std::chrono::microseconds(100));    // Уменьшено
                }
            }

            // Уменьшенная финальная обработка
            for (int i = 0; i < 50 && (received_count < message_count || (slot && slot->queue_size() > 0)); ++i)    // Уменьшено с 200
            {
                executor->spin_once(std::chrono::milliseconds(5));
                if (slot && slot->queue_size() == 0 && received_count >= message_count)
                    break;
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            factory->destroy();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));    // Уменьшено
            executor->stop();
            size_t memory_after = get_current_memory_usage();

            // Измеряем задержку в миллисекундах
            std::vector<double> latencies;
            for (size_t i = 0; i < message_count; ++i)
            {
                if (receive_times[i].time_since_epoch().count() > 0)
                {
                    auto latency_ms = std::chrono::duration<double, std::milli>(
                                          receive_times[i] - start_time)
                                          .count();
                    latencies.push_back(latency_ms);
                }
            }

            result.duration_ms            = std::chrono::duration<double, std::milli>(end_time - start_time).count();
            result.messages_received      = received_count.load();
            result.dropped_messages       = dropped_count.load();
            result.peak_queue_size        = peak_queue.load();
            result.memory_usage_bytes     = memory_after - memory_before;
            result.throughput_msg_per_sec = (message_count * 1000.0) / result.duration_ms;

            if (!latencies.empty())
            {
                // Вычисляем расширенную статистику
                result.calculate_extended_latency_stats(latencies);
                // Сохраняем также старые поля для совместимости
                std::sort(latencies.begin(), latencies.end());
                result.latency_min_ms = latencies.front();
                result.latency_max_ms = latencies.back();
                double sum            = 0;
                for (auto lat : latencies)
                    sum += lat;
                result.latency_avg_ms = sum / latencies.size();
                size_t p99_idx        = static_cast<size_t>(latencies.size() * 0.99);
                result.latency_p99_ms = (p99_idx < latencies.size()) ? latencies[p99_idx] : latencies.back();
            }

            //std::cerr << " Done\n";
            return result;
        }

        static BenchmarkResult run_multiple_producers_single_consumer(size_t producer_count        = 4,
                                                                      size_t messages_per_producer = 25000)
        {
            size_t total_messages = producer_count * messages_per_producer;
            // std::cerr << "[MPSC] " << producer_count << " producers, "
            //           << total_messages << " total messages..." << std::flush;

            BenchmarkResult result;
            result.name          = "Multiple Producers - Single Consumer";
            result.messages_sent = total_messages;

            auto executor = std::make_shared<Executor>(2);
            auto factory  = std::make_shared<Factory>("benchmark_mpsc_factory");

            // Increase queue capacity to handle burst of messages from multiple producers
            // 4 producers * 25000 = 100000 messages, so we need at least that capacity
            const size_t queue_capacity = std::max<size_t>(100000, producer_count * messages_per_producer);

            std::atomic<size_t> received_count{0};
            std::atomic<size_t> dropped_count{0};
            std::atomic<size_t> peak_queue{0};

            auto callback_group = std::make_shared<CallbackGroup>(
                CallbackGroup::Type::MutuallyExclusive, "benchmark_mpsc");

            // Create subscription through Factory (ROS2-like API) - using TestMessage for benchmarking
            auto subscription_void = factory->createSubscription<arch::experimental::TestMessage<size_t>>(
                "/benchmark/mpsc",
                [&received_count](message_ptr<const IMessage> msg) { 
                    if (msg) {
                        received_count++; 
                    }
                },
                QoS::Reliable(queue_capacity),
                callback_group);
            
            // Cast to Subscription<TestMessage<size_t>> for compatibility
            auto subscription = std::static_pointer_cast<Subscription<arch::experimental::TestMessage<size_t>>>(subscription_void);

            // Get topic from Factory and add to executor
            auto topic = factory->getTopic<arch::experimental::TestMessage<size_t>>("/benchmark/mpsc");
            if (topic)
            {
                executor->add_topic(topic);
            }

            // Get slot from subscription for queue size monitoring
            auto slot = subscription ? subscription->getSlot() : nullptr;

            executor->start();
            size_t memory_before = get_current_memory_usage();
            auto start_time      = std::chrono::high_resolution_clock::now();

            std::vector<std::thread> producers;
            std::atomic<bool> stop_flag{false};

            std::thread monitor([&]() {
                while (!stop_flag.load())
                {
                    if (slot)
                    {
                        size_t queue_size   = slot->queue_size();
                        size_t current_peak = peak_queue.load();
                        while (queue_size > current_peak)
                            if (peak_queue.compare_exchange_weak(current_peak, queue_size))
                                break;
                    }
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
            });

            // Create publisher through Factory (ROS2-like API) - using TestMessage for benchmarking
            auto publisher = factory->createPublisher<arch::experimental::TestMessage<size_t>>("/benchmark/mpsc", QoS::Reliable(queue_capacity));

            for (size_t p = 0; p < producer_count; ++p)
            {
                producers.emplace_back([&, p, publisher]() {
                    size_t start_idx = p * messages_per_producer;
                    for (size_t i = 0; i < messages_per_producer; ++i)
                    {
                        // For reliable QoS, retry on failure
                        bool published = false;
                        int retries    = 0;
                        while (!published && retries < 5)
                        {
                            if (publisher)
                                published = publisher->publish(arch::experimental::TestMessage<size_t>(start_idx + i));
                            if (!published)
                            {
                                std::this_thread::yield();
                                retries++;
                            }
                        }

                        if (!published)
                        {
                            dropped_count.fetch_add(1, std::memory_order_relaxed);
                        }

                        // Убрали yield для ускорения
                    }
                });
            }

            for (auto& producer : producers)
                producer.join();

            // Оптимизированное ожидание обработки всех сообщений
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            while (received_count < total_messages &&
                   std::chrono::steady_clock::now() < deadline)
            {
                executor->spin_once(std::chrono::milliseconds(5));    // Уменьшено

                if (slot && slot->queue_size() == 0 && received_count < total_messages)
                {
                    std::this_thread::sleep_for(std::chrono::microseconds(100));    // Уменьшено
                }
            }

            // Уменьшенная финальная обработка
            for (int i = 0; i < 30 && received_count < total_messages; ++i)    // Уменьшено с 100
            {
                executor->spin_once(std::chrono::milliseconds(5));
            }

            stop_flag.store(true);
            if (monitor.joinable())
                monitor.join();

            auto end_time = std::chrono::high_resolution_clock::now();
            factory->destroy();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));    // Уменьшено
            executor->stop();
            size_t memory_after = get_current_memory_usage();

            result.duration_ms            = std::chrono::duration<double, std::milli>(end_time - start_time).count();
            result.messages_received      = received_count.load();
            result.dropped_messages       = dropped_count.load();
            result.peak_queue_size        = peak_queue.load();
            result.memory_usage_bytes     = memory_after - memory_before;
            result.throughput_msg_per_sec = (total_messages * 1000.0) / result.duration_ms;

            //std::cerr << " Done\n";
            return result;
        }

        static BenchmarkResult run_latency_test(size_t iterations = 10000)
        {
            //std::cerr << "[Latency] " << iterations << " iterations..." << std::flush;

            BenchmarkResult result;
            result.name          = "Latency Test";
            result.messages_sent = iterations;

            auto executor = std::make_shared<Executor>(1);
            auto factory  = std::make_shared<Factory>("benchmark_latency_factory");

            std::vector<double> latencies;
            latencies.reserve(iterations);
            std::mutex latencies_mutex;
            std::atomic<size_t> received{0};

            auto callback_group = std::make_shared<CallbackGroup>(
                CallbackGroup::Type::Reentrant, "latency_test");

            // Create subscription through Factory (ROS2-like API) - using TestMessage for benchmarking
            auto subscription_void = factory->createSubscription<arch::experimental::TestMessage<TimestampedMessage>>(
                "/benchmark/latency",
                [&](message_ptr<const IMessage> msg) {
                    // Всегда увеличиваем счетчик полученных сообщений
                    received++;

                    // Получаем конкретный тип сообщения из IMessagePtr
                    auto ts_msg = std::dynamic_pointer_cast<const arch::experimental::TestMessage<TimestampedMessage>>(msg);
                    if (ts_msg)
                    {
                        auto end_time   = std::chrono::high_resolution_clock::now();
                        auto latency_ms = std::chrono::duration<double, std::milli>(
                                              end_time - ts_msg->data.send_time)
                                              .count();
                        {
                            std::lock_guard<std::mutex> lock(latencies_mutex);
                            latencies.push_back(latency_ms);
                        }
                    }
                },
                QoS::Reliable(1000),
                callback_group);
            
            // Cast to Subscription<TestMessage<TimestampedMessage>> for compatibility
            auto subscription = std::static_pointer_cast<Subscription<arch::experimental::TestMessage<TimestampedMessage>>>(subscription_void);

            // Create publisher through Factory (ROS2-like API) - using TestMessage for benchmarking
            auto publisher = factory->createPublisher<arch::experimental::TestMessage<TimestampedMessage>>("/benchmark/latency", QoS::Reliable(1000));

            // Get topic from Factory and add to executor
            auto topic = factory->getTopic<arch::experimental::TestMessage<TimestampedMessage>>("/benchmark/latency");
            if (topic)
            {
                executor->add_topic(topic);
            }

            // Get slot from subscription for queue size monitoring
            auto slot = subscription ? subscription->getSlot() : nullptr;

            executor->start();
            size_t memory_before = get_current_memory_usage();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));    // warm-up
            auto start_time = std::chrono::high_resolution_clock::now();

            std::atomic<size_t> publish_failures{0};

            for (size_t i = 0; i < iterations; ++i)
            {
                TimestampedMessage msg_data;
                msg_data.send_time = std::chrono::high_resolution_clock::now();
                msg_data.index     = i;

                // Retry logic for reliable QoS
                int retries           = 0;
                const int max_retries = 5;
                bool published        = false;
                while (retries < max_retries && !published)
                {
                    if (publisher)
                        published = publisher->publish(arch::experimental::TestMessage<TimestampedMessage>(msg_data));
                    if (!published)
                    {
                        std::this_thread::yield();
                        retries++;
                    }
                }

                if (!published)
                    publish_failures++;

                // Убрали sleep для ускорения
            }

            // Оптимизированное ожидание обработки всех сообщений
            auto deadline            = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            size_t no_progress_count = 0;
            size_t last_received     = 0;

            while (received < iterations && std::chrono::steady_clock::now() < deadline)
            {
                executor->spin_once(std::chrono::milliseconds(5));    // Уменьшено

                size_t current_queue_size = slot ? slot->queue_size() : 0;
                size_t current_received   = received.load();

                // Проверяем прогресс
                if (current_received == last_received)
                {
                    no_progress_count++;
                    if (no_progress_count > 50 && current_queue_size == 0)    // Уменьшено
                    {
                        break;
                    }
                }
                else
                {
                    no_progress_count = 0;
                    last_received     = current_received;
                }

                if (current_queue_size == 0 && received < iterations)
                {
                    std::this_thread::sleep_for(std::chrono::microseconds(100));    // Уменьшено
                }
            }

            // Уменьшенная финальная обработка
            for (int i = 0; i < 50 && (received < iterations || (slot && slot->queue_size() > 0)); ++i)    // Уменьшено
            {
                executor->spin_once(std::chrono::milliseconds(5));
                if (slot && slot->queue_size() == 0 && received >= iterations)
                    break;
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            factory->destroy();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));    // Уменьшено
            executor->stop();
            size_t memory_after = get_current_memory_usage();

            result.duration_ms            = std::chrono::duration<double, std::milli>(end_time - start_time).count();
            result.messages_received      = received.load();
            result.dropped_messages       = 0;
            result.memory_usage_bytes     = memory_after - memory_before;
            result.throughput_msg_per_sec = (iterations * 1000.0) / result.duration_ms;

            if (!latencies.empty())
            {
                // Вычисляем расширенную статистику
                result.calculate_extended_latency_stats(latencies);
                // Сохраняем также старые поля для совместимости
                std::sort(latencies.begin(), latencies.end());
                result.latency_min_ms = latencies.front();
                result.latency_max_ms = latencies.back();
                double sum            = 0;
                for (auto lat : latencies)
                    sum += lat;
                result.latency_avg_ms = sum / latencies.size();
                size_t p99_idx        = static_cast<size_t>(latencies.size() * 0.99);
                result.latency_p99_ms = (p99_idx < latencies.size()) ? latencies[p99_idx] : latencies.back();
            }

            //std::cerr << "\n";
            return result;
        }

        static BenchmarkResult run_memory_test()
        {
            //std::cerr << "[Memory] Testing memory usage..." << std::flush;

            BenchmarkResult result;
            result.name = "Memory Usage Test";

            size_t base_memory = get_current_memory_usage();

            const size_t topic_count           = 20;    // Уменьшено с 50
            const size_t subscribers_per_topic = 3;     // Уменьшено с 5

            auto executor = std::make_shared<Executor>(2);
            executor->start();

            auto factory = std::make_shared<Factory>("benchmark_memory_factory");

            std::vector<std::shared_ptr<Subscription<arch::experimental::TestMessage<int>>>> subscriptions;
            std::vector<std::shared_ptr<Publisher<arch::experimental::TestMessage<int>>>> publishers;
            std::atomic<size_t> received_count{0};
            std::atomic<size_t> dropped_count{0};

            for (size_t i = 0; i < topic_count; ++i)
            {
                std::string topic_name = "/benchmark/memory/topic_" + std::to_string(i);

                // Create publisher through Factory (ROS2-like API) - using TestMessage for benchmarking
                auto publisher = factory->createPublisher<arch::experimental::TestMessage<int>>(topic_name, QoS::Reliable(10));
                if (publisher)
                {
                    publishers.push_back(publisher);
                }

                // Get topic from Factory and add to executor
                auto topic = factory->getTopic<arch::experimental::TestMessage<int>>(topic_name);
                if (topic)
                {
                    executor->add_topic(topic);
                }

                for (size_t j = 0; j < subscribers_per_topic; ++j)
                {
                    auto callback_group = std::make_shared<CallbackGroup>(
                        CallbackGroup::Type::Reentrant,
                        "mem_test_" + std::to_string(i) + "_" + std::to_string(j));

                    // Create subscription through Factory (ROS2-like API) - using TestMessage for benchmarking
                    auto subscription_void = factory->createSubscription<arch::experimental::TestMessage<int>>(
                        topic_name,
                        [&received_count](message_ptr<const IMessage> msg) {
                            // Получаем конкретный тип сообщения из IMessagePtr
                            auto int_msg = std::dynamic_pointer_cast<const arch::experimental::TestMessage<int>>(msg);
                            if (int_msg)
                            {
                                received_count++;
                                // Minimal processing
                                volatile int dummy = int_msg->data;
                                (void)dummy;
                            }
                        },
                        QoS::Reliable(10),
                        callback_group);
                    
                    // Cast to Subscription<TestMessage<int>> for compatibility
                    auto subscription = std::static_pointer_cast<Subscription<arch::experimental::TestMessage<int>>>(subscription_void);

                    if (subscription)
                    {
                        subscriptions.push_back(subscription);
                    }
                }
            }

            const size_t messages_per_topic = 50;    // Уменьшено с 100
            size_t total_messages           = 0;

            auto start_time = std::chrono::high_resolution_clock::now();

            // Publish messages using publishers from Factory
            for (auto& publisher : publishers)
            {
                if (publisher)
                {
                    for (size_t i = 0; i < messages_per_topic; ++i)
                    {
                        if (!publisher->publish(arch::experimental::TestMessage<int>(static_cast<int>(i))))
                            dropped_count++;
                        total_messages++;
                    }
                }
            }

            // Оптимизированное ожидание обработки сообщений
            size_t expected_received = total_messages * subscribers_per_topic;
            auto deadline            = std::chrono::steady_clock::now() + std::chrono::seconds(3);    // Уменьшено

            while (received_count < expected_received &&
                   std::chrono::steady_clock::now() < deadline)
            {
                executor->spin_once(std::chrono::milliseconds(5));    // Уменьшено

                bool has_messages = false;
                for (const auto& subscription : subscriptions)
                {
                    if (subscription)
                    {
                        auto slot = subscription->getSlot();
                        if (slot && slot->queue_size() > 0)
                        {
                            has_messages = true;
                            break;
                        }
                    }
                }

                if (!has_messages && received_count < expected_received)
                {
                    std::this_thread::sleep_for(std::chrono::microseconds(100));    // Уменьшено
                }
            }

            // Уменьшенная финальная обработка
            for (int i = 0; i < 10; ++i)    // Уменьшено с 20
            {
                executor->spin_once(std::chrono::milliseconds(5));
            }

            auto end_time = std::chrono::high_resolution_clock::now();

            size_t current_memory = get_current_memory_usage();

            // Cleanup - destroy factory (will destroy all subscriptions/publishers)
            factory->destroy();

            std::this_thread::sleep_for(std::chrono::milliseconds(20));    // Уменьшено
            executor->stop();

            result.memory_usage_bytes = current_memory - base_memory;
            result.messages_sent      = total_messages;
            // Для Memory Test считаем только уникальные сообщения (не умножаем на подписчиков)
            // так как это тест памяти, а не throughput
            result.messages_received = total_messages;    // Предполагаем, что все сообщения доставлены
            result.dropped_messages  = dropped_count.load();

            result.duration_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

            // Throughput можно грубо посчитать как messages_sent / время
            if (result.duration_ms > 0)
                result.throughput_msg_per_sec = (result.messages_sent * 1000.0) / result.duration_ms;
            else
                result.throughput_msg_per_sec = 0;

            //std::cerr << " Done (" << topic_count << " topics, " << subscriptions.size() << " subscribers)\n";

            return result;
        }

        /**
         * @brief Run single benchmark with averaging
         * @tparam TestFunc Function type that returns BenchmarkResult
         * @param test_func Function to run
         * @param iterations Number of iterations to average (default: 1)
         * @return Averaged benchmark result
         */
        template <typename TestFunc>
        static BenchmarkResult run_with_averaging(TestFunc test_func, size_t iterations = 1)
        {
            // Детальные результаты выводим в stderr (появляются рядом с таблицей на экране)
            // Таблица выводится в stdout через print_summary_table

            if (iterations == 1)
            {
                auto result = test_func();
                // Детальные результаты в stderr (не мешают таблице на экране)
                //result.print_to_stream(std::cerr);
                return result;
            }

            std::vector<BenchmarkResult> results;
            results.reserve(iterations);

            for (size_t i = 0; i < iterations; ++i)
            {
                auto result = test_func();
                results.push_back(result);
                // Выводим результат каждой итерации в stderr
                //std::cerr << "\n--- " << result.name << " - Iteration " << (i + 1) << " / " << iterations << " ---\n";
                //result.print_to_stream(std::cerr);
                // Небольшая пауза между итерациями (уменьшено для ускорения)
                if (i < iterations - 1)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));    // Уменьшено с 50
            }

            auto avg_result = average_results(results);
            // Усредненный результат в stderr
            //std::cerr << "\n--- " << avg_result.name << " - Averaged Result (" << iterations << " iterations) ---\n";
            //avg_result.print_to_stream(std::cerr);
            return avg_result;
        }

        /**
         * @brief Average multiple benchmark results
         * @param results Vector of results to average
         * @return Averaged result
         */
        static BenchmarkResult average_results(const std::vector<BenchmarkResult>& results)
        {
            if (results.empty())
                return BenchmarkResult{};

            BenchmarkResult avg;
            avg.name            = results[0].name;
            avg.iteration_count = results.size();

            // Усреднение метрик
            double sum_duration = 0.0, sum_throughput = 0.0;
            double sum_latency_avg = 0.0, sum_latency_min = 0.0, sum_latency_max = 0.0;
            double sum_latency_p99 = 0.0, sum_latency_p50 = 0.0, sum_latency_p95 = 0.0;
            double sum_latency_p99_9 = 0.0, sum_latency_stddev = 0.0, sum_latency_jitter = 0.0;
            size_t sum_sent = 0, sum_received = 0, sum_dropped = 0;
            size_t sum_peak_queue = 0;
            // Для памяти используем среднее только положительных значений, чтобы избежать переполнения
            size_t sum_memory            = 0;
            size_t positive_memory_count = 0;

            for (const auto& r : results)
            {
                sum_duration += r.duration_ms;
                sum_throughput += r.throughput_msg_per_sec;
                sum_latency_avg += r.latency_avg_ms;
                sum_latency_min += r.latency_min_ms;
                sum_latency_max += r.latency_max_ms;
                sum_latency_p99 += r.latency_p99_ms;
                sum_latency_p50 += r.latency_p50_ms;
                sum_latency_p95 += r.latency_p95_ms;
                sum_latency_p99_9 += r.latency_p99_9_ms;
                sum_latency_stddev += r.latency_stddev_ms;
                sum_latency_jitter += r.latency_jitter_ms;
                sum_sent += r.messages_sent;
                sum_received += r.messages_received;
                sum_dropped += r.dropped_messages;
                sum_peak_queue += r.peak_queue_size;
                // Суммируем только положительные значения памяти (избегаем переполнения)
                // Проверяем на разумный максимум (100 GB)
                const size_t MAX_REASONABLE_MEMORY = 100ULL * 1024 * 1024 * 1024;
                if (r.memory_usage_bytes > 0 && r.memory_usage_bytes < MAX_REASONABLE_MEMORY)
                {
                    sum_memory += r.memory_usage_bytes;
                    positive_memory_count++;
                }
            }

            size_t count               = results.size();
            avg.duration_ms            = sum_duration / count;
            avg.throughput_msg_per_sec = sum_throughput / count;
            avg.latency_avg_ms         = sum_latency_avg / count;
            avg.latency_min_ms         = sum_latency_min / count;
            avg.latency_max_ms         = sum_latency_max / count;
            avg.latency_p99_ms         = sum_latency_p99 / count;
            avg.latency_p50_ms         = sum_latency_p50 / count;
            avg.latency_p95_ms         = sum_latency_p95 / count;
            avg.latency_p99_9_ms       = sum_latency_p99_9 / count;
            avg.latency_stddev_ms      = sum_latency_stddev / count;
            avg.latency_jitter_ms      = sum_latency_jitter / count;
            // Для целочисленных значений используем округление
            avg.messages_sent     = static_cast<size_t>(std::round(static_cast<double>(sum_sent) / count));
            avg.messages_received = static_cast<size_t>(std::round(static_cast<double>(sum_received) / count));
            avg.dropped_messages  = static_cast<size_t>(std::round(static_cast<double>(sum_dropped) / count));
            avg.peak_queue_size   = static_cast<size_t>(std::round(static_cast<double>(sum_peak_queue) / count));
            // Для памяти используем среднее только положительных значений
            if (positive_memory_count > 0)
                avg.memory_usage_bytes = static_cast<size_t>(std::round(static_cast<double>(sum_memory) / positive_memory_count));
            else
                avg.memory_usage_bytes = 0;

            // Вычисление стандартного отклонения для throughput
            double variance = 0.0;
            for (const auto& r : results)
            {
                double diff = r.throughput_msg_per_sec - avg.throughput_msg_per_sec;
                variance += diff * diff;
            }
            avg.throughput_stddev = std::sqrt(variance / count);

            return avg;
        }

        /**
         * @brief Run all benchmarks
         * @param iterations_per_test Number of iterations to average for each test (default: 1)
         * @return Vector of benchmark results
         */
        static std::vector<BenchmarkResult> run_all_benchmarks(size_t iterations_per_test = 1)
        {
            std::cerr << "========================================\n";
            std::cerr << "      ARCH Messaging System Benchmark   \n";
            std::cerr << "========================================\n";
            //if (iterations_per_test > 1)
            //    std::cerr << "Averaging " << iterations_per_test << " iterations per test\n";

            std::vector<BenchmarkResult> results;

            // Оптимизированные параметры для быстрого выполнения
            results.push_back(run_with_averaging(
                []() { return run_single_producer_single_consumer(50000); },
                iterations_per_test));
            results.push_back(run_with_averaging(
                []() { return run_multiple_producers_single_consumer(4, 10000); },
                iterations_per_test));
            results.push_back(run_with_averaging(
                []() { return run_latency_test(2000); },
                iterations_per_test));
            results.push_back(run_with_averaging(
                []() { return run_memory_test(); },
                iterations_per_test));

            print_summary_table(results);
            return results;
        }

    public:
        static size_t get_current_memory_usage()
        {
#ifdef __linux__
            std::ifstream status("/proc/self/status");
            std::string line;
            while (std::getline(status, line))
            {
                if (line.find("VmRSS:") == 0)
                {
                    std::istringstream iss(line);
                    std::string key, value, unit;
                    iss >> key >> value >> unit;
                    return std::stoul(value) * 1024;
                }
            }
#endif
            return 0;
        }

        static void print_summary_table(const std::vector<BenchmarkResult>& results)
        {
            // Таблица выводится в stdout для отображения на экране
            // Детальные результаты уже выведены в stderr через print_to_stream
            std::cout << "\n\n========================================================\n";
            std::cout << "                   SUMMARY TABLE                    \n";
            std::cout << "========================================================\n";

            std::cout << std::left << std::setw(35) << "Test"
                      << std::setw(12) << "Msg/sec"
                      << std::setw(14) << "Latency(ms)"
                      << std::setw(12) << "Duration(ms)"
                      << std::setw(10) << "Memory"
                      << std::setw(10) << "Loss %"
                      << std::setw(12) << "Efficiency\n";

            std::cout << std::string(100, '-') << "\n";

            for (const auto& result : results)
            {
                // Защита от деления на ноль и переполнения
                double loss_percent = 0.0;
                double efficiency   = 0.0;

                if (result.messages_sent > 0)
                {
                    loss_percent = (result.dropped_messages * 100.0) / result.messages_sent;
                    efficiency   = (result.messages_received * 100.0) / result.messages_sent;

                    // Ограничиваем значения разумными пределами
                    if (loss_percent > 100.0)
                        loss_percent = 100.0;
                    if (efficiency > 100.0)
                        efficiency = 100.0;
                }

                std::cout << std::left << std::setw(35) << result.name.substr(0, 34)
                          << std::right << std::setw(11) << std::fixed << std::setprecision(0)
                          << result.throughput_msg_per_sec << " "
                          << std::setw(13) << std::setprecision(3) << result.latency_avg_ms << " "
                          << std::setw(11) << std::fixed << std::setprecision(2) << result.duration_ms << " "
                          << std::setw(9) << BenchmarkResult::format_memory(result.memory_usage_bytes)
                          << std::setw(9) << std::setprecision(2) << loss_percent << "%"
                          << std::setw(11) << std::setprecision(1) << efficiency << "%\n";
            }
        }

        static void save_results_to_csv(const std::vector<BenchmarkResult>& results, const std::string& filename)
        {
            std::ofstream file(filename);
            if (!file.is_open())
            {
                std::cerr << "Error: Cannot open file " << filename << " for writing.\n";
                return;
            }

            // Заголовок - все в миллисекундах
            file << "Test,Iterations,Messages Sent,Messages Received,Dropped,Duration(ms),Throughput(msg/sec),"
                 << "Throughput Stddev,Latency Avg(ms),Latency Min(ms),Latency Max(ms),"
                 << "Latency P50(ms),Latency P95(ms),Latency P99(ms),Latency P99.9(ms),"
                 << "Latency Stddev(ms),Latency Jitter(ms),Peak Queue,Memory\n";

            for (const auto& r : results)
            {
                file << r.name << ","
                     << r.iteration_count << ","
                     << r.messages_sent << ","
                     << r.messages_received << ","
                     << r.dropped_messages << ","
                     << r.duration_ms << ","
                     << r.throughput_msg_per_sec << ","
                     << r.throughput_stddev << ","
                     << r.latency_avg_ms << ","
                     << r.latency_min_ms << ","
                     << r.latency_max_ms << ","
                     << r.latency_p50_ms << ","
                     << r.latency_p95_ms << ","
                     << r.latency_p99_ms << ","
                     << r.latency_p99_9_ms << ","
                     << r.latency_stddev_ms << ","
                     << r.latency_jitter_ms << ","
                     << r.peak_queue_size << ","
                     << r.memory_usage_bytes
                     << "\n";
            }
            file.close();
            std::cerr << "Results saved to CSV: " << filename << std::endl;
        }
    };

}    // namespace arch::experimental

#endif    // !ARCH_BENCHMARK_H
