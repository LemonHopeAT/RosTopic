/**
 * @file 03_qos_example.cpp
 * @brief Пример использования различных настроек QoS
 * 
 * Этот пример демонстрирует:
 * - BestEffort vs Reliable QoS
 * - Различные размеры очередей
 * - SingleConsumer режим доставки
 */

#include "experimental/test_message.h"
#include <arch/communication/imessage.h>
#include <communication/impl/executor.h>
#include <communication/impl/factory.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

using namespace arch::experimental;

struct SensorData
{
    double temperature;
    double humidity;
    int timestamp;

    SensorData() : temperature(0.0), humidity(0.0), timestamp(0) {}
    SensorData(double t, double h, int ts) : temperature(t), humidity(h), timestamp(ts) {}
};

int main()
{
    std::cout << "=== Пример использования QoS ===\n\n";

    auto factory  = std::make_shared<Factory>("qos_example_node");
    auto executor = std::make_shared<Executor>(1);

    std::atomic<int> reliable_received{0};
    std::atomic<int> besteffort_received{0};

    // Пример 1: Reliable QoS - сообщения гарантированно доставляются
    std::cout << "--- Пример 1: Reliable QoS ---\n";
    auto reliable_sub = factory->createSubscription<TestMessage<SensorData>>(
        "/sensors/reliable",
        [&reliable_received](arch::message_ptr<const arch::IMessage> msg) {
            if (msg)
            {
                const auto* data = static_cast<const TestMessage<SensorData>*>(msg.get());
                if (data)
                {
                    reliable_received++;
                    std::cout << "[Reliable] Получено: temp=" << data->data.temperature
                              << ", humidity=" << data->data.humidity
                              << ", ts=" << data->data.timestamp << "\n";
                }
            }
        },
        QoS::Reliable(50),    // Надежная доставка, очередь на 50 сообщений
        nullptr,
        "");

    auto reliable_pub = factory->createPublisher<TestMessage<SensorData>>(
        "/sensors/reliable",
        QoS::Reliable(50)    // Должен совпадать с подпиской
    );

    auto reliable_topic = factory->getTopic<TestMessage<SensorData>>("/sensors/reliable");
    if (reliable_topic)
    {
        executor->add_topic(reliable_topic);
    }

    // Пример 2: BestEffort QoS - сообщения могут быть потеряны при переполнении
    std::cout << "\n--- Пример 2: BestEffort QoS ---\n";
    auto besteffort_sub = factory->createSubscription<TestMessage<SensorData>>(
        "/sensors/besteffort",
        [&besteffort_received](arch::message_ptr<const arch::IMessage> msg) {
            if (msg)
            {
                const auto* data = static_cast<const TestMessage<SensorData>*>(msg.get());
                if (data)
                {
                    besteffort_received++;
                    std::cout << "[BestEffort] Получено: temp=" << data->data.temperature
                              << ", humidity=" << data->data.humidity
                              << ", ts=" << data->data.timestamp << "\n";
                }
            }
        },
        QoS::BestEffort(10),    // Лучшие усилия, очередь на 10 сообщений
        nullptr,
        "");

    auto besteffort_pub = factory->createPublisher<TestMessage<SensorData>>(
        "/sensors/besteffort",
        QoS::BestEffort(10));

    auto besteffort_topic = factory->getTopic<TestMessage<SensorData>>("/sensors/besteffort");
    if (besteffort_topic)
    {
        executor->add_topic(besteffort_topic);
    }

    executor->start();

    // Отправляем сообщения в Reliable топик
    std::cout << "\nОтправка 5 сообщений в Reliable топик...\n";
    for (int i = 0; i < 5; ++i)
    {
        SensorData data(20.0 + i, 50.0 + i, i);
        TestMessage<SensorData> msg(data);
        reliable_pub->publish(msg);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Отправляем сообщения в BestEffort топик
    std::cout << "\nОтправка 5 сообщений в BestEffort топик...\n";
    for (int i = 0; i < 5; ++i)
    {
        SensorData data(25.0 + i, 55.0 + i, i);
        TestMessage<SensorData> msg(data);
        besteffort_pub->publish(msg);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Обрабатываем сообщения
    std::cout << "\nОбработка сообщений...\n";
    for (int i = 0; i < 20; ++i)
    {
        executor->spin_some();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::cout << "\n=== Результаты ===\n";
    std::cout << "Reliable получено: " << reliable_received.load() << " сообщений\n";
    std::cout << "BestEffort получено: " << besteffort_received.load() << " сообщений\n";

    executor->stop();
    factory->destroy();

    return 0;
}
