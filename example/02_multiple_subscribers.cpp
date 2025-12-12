/**
 * @file 02_multiple_subscribers.cpp
 * @brief Пример с несколькими подписчиками на один топик
 * 
 * Этот пример демонстрирует:
 * - Создание нескольких подписок на один топик
 * - Broadcast режим доставки (сообщение получают все подписчики)
 * - Использование CallbackGroup для синхронизации
 */

#include "experimental/test_message.h"
#include <arch/communication/imessage.h>
#include <communication/impl/callback_group.h>
#include <communication/impl/executor.h>
#include <communication/impl/factory.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

using namespace arch::experimental;

struct CounterData
{
    int counter;
    std::string source;

    CounterData() : counter(0), source("") {}
    CounterData(int c, const std::string& s) : counter(c), source(s) {}
};

int main()
{
    std::cout << "=== Пример с несколькими подписчиками ===\n\n";

    auto factory  = std::make_shared<Factory>("multi_subscriber_node");
    auto executor = std::make_shared<Executor>(2);    // 2 потока для обработки

    // Счетчики для каждого подписчика
    std::atomic<int> subscriber1_count{0};
    std::atomic<int> subscriber2_count{0};
    std::atomic<int> subscriber3_count{0};

    // Создаем CallbackGroup для синхронизации
    auto callback_group = std::make_shared<CallbackGroup>(
        CallbackGroup::Type::Reentrant,
        "example_group");

    // Создаем три подписки на один топик
    auto sub1 = factory->createSubscription<TestMessage<CounterData>>(
        "/example/counter",
        [&subscriber1_count](arch::message_ptr<const arch::IMessage> msg) {
            if (msg)
            {
                const auto* data = static_cast<const TestMessage<CounterData>*>(msg.get());
                if (data)
                {
                    subscriber1_count++;
                    std::cout << "[Subscriber 1] Получено: counter=" << data->data.counter
                              << ", source=\"" << data->data.source << "\"\n";
                }
            }
        },
        QoS::Reliable(100),
        callback_group,
        "");

    auto sub2 = factory->createSubscription<TestMessage<CounterData>>(
        "/example/counter",
        [&subscriber2_count](arch::message_ptr<const arch::IMessage> msg) {
            if (msg)
            {
                const auto* data = static_cast<const TestMessage<CounterData>*>(msg.get());
                if (data)
                {
                    subscriber2_count++;
                    std::cout << "[Subscriber 2] Получено: counter=" << data->data.counter
                              << ", source=\"" << data->data.source << "\"\n";
                }
            }
        },
        QoS::Reliable(100),
        callback_group,
        "");

    auto sub3 = factory->createSubscription<TestMessage<CounterData>>(
        "/example/counter",
        [&subscriber3_count](arch::message_ptr<const arch::IMessage> msg) {
            if (msg)
            {
                const auto* data = static_cast<const TestMessage<CounterData>*>(msg.get());
                if (data)
                {
                    subscriber3_count++;
                    std::cout << "[Subscriber 3] Получено: counter=" << data->data.counter
                              << ", source=\"" << data->data.source << "\"\n";
                }
            }
        },
        QoS::Reliable(100),
        callback_group,
        "");

    // Добавляем топик в Executor
    auto topic = factory->getTopic<TestMessage<CounterData>>("/example/counter");
    if (topic)
    {
        executor->add_topic(topic);
    }

    executor->start();

    // Создаем Publisher
    auto publisher = factory->createPublisher<TestMessage<CounterData>>(
        "/example/counter",
        QoS::Reliable(100));

    std::cout << "Отправка 10 сообщений...\n\n";

    // Отправляем сообщения
    for (int i = 1; i <= 10; ++i)
    {
        CounterData data(i, "Publisher");
        TestMessage<CounterData> msg(data);

        if (publisher->publish(msg))
        {
            std::cout << "Отправлено сообщение #" << i << "\n";
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Обрабатываем сообщения
    std::cout << "\nОбработка сообщений...\n";
    for (int i = 0; i < 20; ++i)
    {
        executor->spin_some();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Ожидаем завершения обработки
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::cout << "\n=== Результаты ===\n";
    std::cout << "Subscriber 1 получил: " << subscriber1_count.load() << " сообщений\n";
    std::cout << "Subscriber 2 получил: " << subscriber2_count.load() << " сообщений\n";
    std::cout << "Subscriber 3 получил: " << subscriber3_count.load() << " сообщений\n";
    std::cout << "\nВсе подписчики должны получить все 10 сообщений (Broadcast режим)\n";

    executor->stop();
    factory->destroy();

    return 0;
}
