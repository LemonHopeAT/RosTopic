#ifndef ARCH_BENCHMARK_H
#define ARCH_BENCHMARK_H

#include "CallbackGroup.h"
#include "Executor.h"
#include "Publisher.h"
#include "SubscriberSlot.h"
#include "Topic.h"
#include "arch/utils.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

#ifdef __linux__
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace arch
{

    class Benchmark
    {
    public:
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

            void print() const
            {
                std::cout << "\n=== " << name << " ===\n";
                std::cout << "Sent: " << messages_sent << "\n";
                std::cout << "Received: " << messages_received << "\n";
                std::cout << "Dropped: " << dropped_messages << " ("
                          << std::fixed << std::setprecision(2)
                          << (dropped_messages * 100.0 / messages_sent) << "%)\n";
                std::cout << "Duration: " << duration_ms << " ms\n";
                std::cout << "Throughput: "
                          << std::fixed << std::setprecision(0)
                          << throughput_msg_per_sec << " msg/sec\n";
                std::cout << "Latency avg: " << latency_avg_ms << " ms\n";
                std::cout << "Latency min: " << latency_min_ms << " ms\n";
                std::cout << "Latency max: " << latency_max_ms << " ms\n";
                std::cout << "Latency p99: " << latency_p99_ms << " ms\n";
                std::cout << "Peak queue: " << peak_queue_size << "\n";
                std::cout << "Memory: " << format_memory(memory_usage_bytes) << "\n";
            }

            static std::string format_memory(size_t bytes)
            {
                if (bytes == 0)
                    return "0 B";

                const char* units[] = {"B", "KB", "MB", "GB"};
                size_t unit_index   = 0;
                double value        = static_cast<double>(bytes);

                while (value >= 1024.0 && unit_index < 3)
                {
                    value /= 1024.0;
                    unit_index++;
                }

                if (value > 10000 && unit_index == 3)    // Больше 10 GB маловероятно для бенчмарка
                {
                    return "N/A";
                }

                std::stringstream ss;
                ss << std::fixed << std::setprecision(2) << value << " " << units[unit_index];
                return ss.str();
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
            std::cout << "[SPSC] Sending " << message_count << " messages..." << std::flush;

            BenchmarkResult result;
            result.name          = "Single Producer - Single Consumer";
            result.messages_sent = message_count;

            auto executor = std::make_shared<Executor>(1);
            auto topic    = std::make_shared<Topic<int>>("/benchmark/spsc", QoS::Reliable(1000));
            executor->add_topic(topic);

            std::atomic<size_t> received_count{0};
            std::atomic<size_t> dropped_count{0};
            std::atomic<size_t> peak_queue{0};
            std::vector<std::chrono::high_resolution_clock::time_point> receive_times(message_count);

            auto callback_group = std::make_shared<CallbackGroup>(
                CallbackGroup::Type::Reentrant, "benchmark_spsc");

            auto slot = topic->subscribe(
                [&received_count, &receive_times](auto msg) {
                    if (msg && msg->data < static_cast<int>(receive_times.size()) && msg->data >= 0)
                    {
                        size_t idx         = static_cast<size_t>(msg->data);
                        receive_times[idx] = std::chrono::high_resolution_clock::now();
                    }
                    received_count++;
                },
                callback_group,
                QoS::Reliable(1000));

            executor->start();

            size_t memory_before = get_current_memory_usage();
            auto start_time      = std::chrono::high_resolution_clock::now();

            std::this_thread::sleep_for(std::chrono::milliseconds(10));    // warm-up

            Publisher<int> publisher(topic);
            for (size_t i = 0; i < message_count; ++i)
            {
                if (!publisher.publish(static_cast<int>(i)))
                    dropped_count++;

                size_t queue_size = slot->queue_size();
                if (queue_size > peak_queue.load())
                    peak_queue.store(queue_size);

                if (i % 1000 == 0 && i > 0)
                    std::this_thread::yield();
            }

            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (received_count < message_count &&
                   std::chrono::steady_clock::now() < deadline)
            {
                executor->spin_once(std::chrono::milliseconds(1));
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            topic->destroy();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
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

            std::cout << " Done\n";
            return result;
        }

        static BenchmarkResult run_multiple_producers_single_consumer(size_t producer_count        = 4,
                                                                      size_t messages_per_producer = 25000)
        {
            size_t total_messages = producer_count * messages_per_producer;
            std::cout << "[MPSC] " << producer_count << " producers, "
                      << total_messages << " total messages..." << std::flush;

            BenchmarkResult result;
            result.name          = "Multiple Producers - Single Consumer";
            result.messages_sent = total_messages;

            auto executor = std::make_shared<Executor>(2);
            auto topic    = std::make_shared<Topic<size_t>>("/benchmark/mpsc", QoS::Reliable(10000));
            executor->add_topic(topic);

            std::atomic<size_t> received_count{0};
            std::atomic<size_t> dropped_count{0};
            std::atomic<size_t> peak_queue{0};

            auto callback_group = std::make_shared<CallbackGroup>(
                CallbackGroup::Type::MutuallyExclusive, "benchmark_mpsc");

            auto slot = topic->subscribe(
                [&received_count](auto msg) { received_count++; },
                callback_group,
                QoS::Reliable(10000));

            executor->start();
            size_t memory_before = get_current_memory_usage();
            auto start_time      = std::chrono::high_resolution_clock::now();

            std::vector<std::thread> producers;
            std::atomic<bool> stop_flag{false};

            std::thread monitor([&]() {
                while (!stop_flag.load())
                {
                    size_t queue_size   = slot->queue_size();
                    size_t current_peak = peak_queue.load();
                    while (queue_size > current_peak)
                        if (peak_queue.compare_exchange_weak(current_peak, queue_size))
                            break;
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
            });

            for (size_t p = 0; p < producer_count; ++p)
            {
                producers.emplace_back([&, p]() {
                    Publisher<size_t> publisher(topic);
                    size_t start_idx = p * messages_per_producer;
                    for (size_t i = 0; i < messages_per_producer; ++i)
                    {
                        if (!publisher.publish(start_idx + i))
                            dropped_count.fetch_add(1, std::memory_order_relaxed);
                        if (i % 1000 == 0)
                            std::this_thread::yield();
                    }
                });
            }

            for (auto& producer : producers)
                producer.join();

            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (received_count < total_messages &&
                   std::chrono::steady_clock::now() < deadline)
            {
                executor->spin_once(std::chrono::milliseconds(1));
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }

            stop_flag.store(true);
            if (monitor.joinable())
                monitor.join();

            auto end_time = std::chrono::high_resolution_clock::now();
            topic->destroy();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            executor->stop();
            size_t memory_after = get_current_memory_usage();

            result.duration_ms            = std::chrono::duration<double, std::milli>(end_time - start_time).count();
            result.messages_received      = received_count.load();
            result.dropped_messages       = dropped_count.load();
            result.peak_queue_size        = peak_queue.load();
            result.memory_usage_bytes     = memory_after - memory_before;
            result.throughput_msg_per_sec = (total_messages * 1000.0) / result.duration_ms;

            std::cout << " Done\n";
            return result;
        }

        static BenchmarkResult run_latency_test(size_t iterations = 10000)
        {
            std::cout << "[Latency] " << iterations << " iterations..." << std::flush;

            BenchmarkResult result;
            result.name          = "Latency Test";
            result.messages_sent = iterations;

            auto executor = std::make_shared<Executor>(1);
            auto topic    = std::make_shared<Topic<TimestampedMessage>>(
                "/benchmark/latency", QoS::Reliable(1000));
            executor->add_topic(topic);

            std::vector<double> latencies;
            latencies.reserve(iterations);
            std::mutex latencies_mutex;
            std::atomic<size_t> received{0};

            auto callback_group = std::make_shared<CallbackGroup>(
                CallbackGroup::Type::Reentrant, "latency_test");

            auto slot = topic->subscribe(
                [&](auto msg) {
                    if (!msg)
                        return;
                    auto end_time   = std::chrono::high_resolution_clock::now();
                    auto latency_ms = std::chrono::duration<double, std::milli>(
                                          end_time - msg->data.send_time)
                                          .count();
                    {
                        std::lock_guard<std::mutex> lock(latencies_mutex);
                        latencies.push_back(latency_ms);
                    }
                    received++;
                },
                callback_group,
                QoS::Reliable(1000));

            executor->start();
            size_t memory_before = get_current_memory_usage();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));    // warm-up
            auto start_time = std::chrono::high_resolution_clock::now();

            Publisher<TimestampedMessage> publisher(topic);
            for (size_t i = 0; i < iterations; ++i)
            {
                TimestampedMessage msg;
                msg.send_time = std::chrono::high_resolution_clock::now();
                msg.index     = i;
                publisher.publish(msg);
                if (i % 100 == 0)
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
            }

            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (received < iterations && std::chrono::steady_clock::now() < deadline)
                executor->spin_once(std::chrono::milliseconds(1));

            auto end_time = std::chrono::high_resolution_clock::now();
            topic->destroy();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            executor->stop();
            size_t memory_after = get_current_memory_usage();

            if (!latencies.empty())
            {
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

            result.duration_ms            = std::chrono::duration<double, std::milli>(end_time - start_time).count();
            result.messages_received      = received.load();
            result.dropped_messages       = 0;
            result.memory_usage_bytes     = memory_after - memory_before;
            result.throughput_msg_per_sec = (iterations * 1000.0) / result.duration_ms;

            std::cout << "\n";
            return result;
        }

        static BenchmarkResult run_memory_test()
        {
            std::cout << "[Memory] Testing memory usage..." << std::flush;

            BenchmarkResult result;
            result.name = "Memory Usage Test";

            size_t base_memory = get_current_memory_usage();

            const size_t topic_count           = 50;
            const size_t subscribers_per_topic = 5;
            size_t dropped_messages            = 0;

            auto executor = std::make_shared<Executor>(2);
            executor->start();

            std::vector<std::shared_ptr<Topic<int>>> topics;
            std::vector<std::shared_ptr<SubscriberSlot<int>>> slots;

            for (size_t i = 0; i < topic_count; ++i)
            {
                auto topic = std::make_shared<Topic<int>>(
                    "/benchmark/memory/topic_" + std::to_string(i), QoS::Reliable(10));

                topics.push_back(topic);
                executor->add_topic(topic);

                for (size_t j = 0; j < subscribers_per_topic; ++j)
                {
                    auto callback_group = std::make_shared<CallbackGroup>(
                        CallbackGroup::Type::Reentrant,
                        "mem_test_" + std::to_string(i) + "_" + std::to_string(j));

                    auto slot = topic->subscribe(
                        [](auto msg) {
                            // Minimal processing
                            volatile int dummy = msg->data;
                            (void)dummy;
                        },
                        callback_group,
                        QoS::Reliable(10));
                    slots.push_back(slot);
                }
            }

            const size_t messages_per_topic = 100;
            size_t total_messages           = 0;

            auto start_time = std::chrono::high_resolution_clock::now();

            for (auto& topic : topics)
            {
                Publisher<int> publisher(topic);
                for (size_t i = 0; i < messages_per_topic; ++i)
                {
                    publisher.publish(static_cast<int>(i));
                    total_messages++;
                }
            }

            // Process messages
            for (int i = 0; i < 50; ++i)
            {
                executor->spin_once(std::chrono::milliseconds(10));
            }

            auto end_time = std::chrono::high_resolution_clock::now();

            size_t current_memory = get_current_memory_usage();

            // Cleanup
            for (auto& topic : topics)
            {
                topic->destroy();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            executor->stop();

            result.memory_usage_bytes = current_memory - base_memory;
            result.messages_sent      = total_messages;
            result.messages_received  = total_messages;

            result.duration_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

            // Throughput можно грубо посчитать как messages_sent / время
            if (result.duration_ms > 0)
                result.throughput_msg_per_sec = (result.messages_sent * 1000.0) / result.duration_ms;
            else
                result.throughput_msg_per_sec = 0;

            std::cout << " Done (" << topics.size() << " topics, " << slots.size() << " subscribers)\n";

            return result;
        }

        static std::vector<BenchmarkResult> run_all_benchmarks()
        {
            std::cout << "========================================\n";
            std::cout << "      ARCH Messaging System Benchmark   \n";
            std::cout << "========================================\n";

            std::vector<BenchmarkResult> results;

            results.push_back(run_single_producer_single_consumer(100000));
            results.push_back(run_multiple_producers_single_consumer(4, 25000));
            results.push_back(run_latency_test(5000));
            results.push_back(run_memory_test());

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
                double loss_percent = (result.dropped_messages * 100.0) / result.messages_sent;
                double efficiency   = (result.messages_received * 100.0) / result.messages_sent;

                std::cout << std::left << std::setw(35) << result.name.substr(0, 34)
                          << std::right << std::setw(11) << std::fixed << std::setprecision(0)
                          << result.throughput_msg_per_sec << " "
                          << std::setw(13) << std::setprecision(3) << result.latency_avg_ms << " "
                          << std::setw(11) << std::fixed << std::setprecision(2) << result.duration_ms << " "
                          << std::setw(9) << result.BenchmarkResult::format_memory(result.memory_usage_bytes)
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
            file << "Test,Messages Sent,Messages Received,Dropped,Duration(ms),Throughput(msg/sec),"
                 << "Latency Avg(ms),Latency Min(ms),Latency Max(ms),Latency P99(ms),"
                 << "Peak Queue,Memory\n";

            for (const auto& r : results)
            {
                file << r.name << ","
                     << r.messages_sent << ","
                     << r.messages_received << ","
                     << r.dropped_messages << ","
                     << r.duration_ms << ","
                     << r.throughput_msg_per_sec << ","
                     << r.latency_avg_ms << ","
                     << r.latency_min_ms << ","
                     << r.latency_max_ms << ","
                     << r.latency_p99_ms << ","
                     << r.peak_queue_size << ","
                     << r.memory_usage_bytes
                     << "\n";
            }
            file.close();
            std::cout << "Results saved to CSV: " << filename << std::endl;
        }
    };

}    // namespace arch

#endif    // ARCH_BENCHMARK_H
