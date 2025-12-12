/**
 * @file 04_single_consumer.cpp
 * @brief Пример использования SingleConsumer режима доставки
 * 
 * Этот пример демонстрирует:
 * - SingleConsumer режим (round-robin доставка между подписчиками)
 * - Использование consumer_group для группировки подписчиков
 * - Различие между Broadcast и SingleConsumer режимами
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

struct TaskData
{
    int task_id;
    std::string description;

    TaskData() : task_id(0), description("") {}
    TaskData(int id, const std::string& desc) : task_id(id), description(desc) {}
};

int main()
{
    std::cout << "=== Пример SingleConsumer режима доставки ===\n\n";

    auto factory  = std::make_shared<Factory>("single_consumer_node");
    auto executor = std::make_shared<Executor>(2);

    std::atomic<int> worker1_received{0};
    std::atomic<int> worker2_received{0};
    std::atomic<int> worker3_received{0};

    // Создаем QoS с SingleConsumer режимом
    QoS single_consumer_qos         = QoS::SingleConsumer(100);
    single_consumer_qos.reliability = QoS::Reliability::Reliable;

    // Создаем три подписчика в одной consumer group
    // В SingleConsumer режиме каждое сообщение доставляется только одному подписчику (round-robin)
    auto worker1 = factory->createSubscription<TestMessage<TaskData>>(
        "/tasks/queue",
        [&worker1_received](arch::message_ptr<const arch::IMessage> msg) {
            if (msg)
            {
                const auto* data = static_cast<const TestMessage<TaskData>*>(msg.get());
                if (data)
                {
                    worker1_received++;
                    std::cout << "[Worker 1] Обрабатывает задачу #" << data->data.task_id
                              << ": " << data->data.description << "\n";
                }
            }
        },
        single_consumer_qos,
        nullptr,
        "workers"    // Все в одной consumer group
    );

    auto worker2 = factory->createSubscription<TestMessage<TaskData>>(
        "/tasks/queue",
        [&worker2_received](arch::message_ptr<const arch::IMessage> msg) {
            if (msg)
            {
                const auto* data = static_cast<const TestMessage<TaskData>*>(msg.get());
                if (data)
                {
                    worker2_received++;
                    std::cout << "[Worker 2] Обрабатывает задачу #" << data->data.task_id
                              << ": " << data->data.description << "\n";
                }
            }
        },
        single_consumer_qos,
        nullptr,
        "workers"    // Та же consumer group
    );

    auto worker3 = factory->createSubscription<TestMessage<TaskData>>(
        "/tasks/queue",
        [&worker3_received](arch::message_ptr<const arch::IMessage> msg) {
            if (msg)
            {
                const auto* data = static_cast<const TestMessage<TaskData>*>(msg.get());
                if (data)
                {
                    worker3_received++;
                    std::cout << "[Worker 3] Обрабатывает задачу #" << data->data.task_id
                              << ": " << data->data.description << "\n";
                }
            }
        },
        single_consumer_qos,
        nullptr,
        "workers"    // Та же consumer group
    );

    auto topic = factory->getTopic<TestMessage<TaskData>>("/tasks/queue");
    if (topic)
    {
        executor->add_topic(topic);
    }

    executor->start();

    auto publisher = factory->createPublisher<TestMessage<TaskData>>(
        "/tasks/queue",
        single_consumer_qos);

    std::cout << "Отправка 9 задач в очередь...\n";
    std::cout << "В SingleConsumer режиме каждая задача будет обработана только одним worker'ом\n\n";

    // Отправляем задачи
    for (int i = 1; i <= 9; ++i)
    {
        TaskData task(i, "Задача #" + std::to_string(i));
        TestMessage<TaskData> msg(task);

        if (publisher->publish(msg))
        {
            std::cout << "Отправлена задача #" << i << "\n";
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Обрабатываем сообщения
    std::cout << "\nОбработка задач...\n";
    for (int i = 0; i < 30; ++i)
    {
        executor->spin_some();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::cout << "\n=== Результаты ===\n";
    std::cout << "Worker 1 обработал: " << worker1_received.load() << " задач\n";
    std::cout << "Worker 2 обработал: " << worker2_received.load() << " задач\n";
    std::cout << "Worker 3 обработал: " << worker3_received.load() << " задач\n";
    std::cout << "Всего обработано: "
              << (worker1_received.load() + worker2_received.load() + worker3_received.load())
              << " задач\n";
    std::cout << "\nВ SingleConsumer режиме каждая задача обрабатывается только одним worker'ом\n";
    std::cout << "Распределение происходит по принципу round-robin\n";

    executor->stop();
    factory->destroy();

    return 0;
}
