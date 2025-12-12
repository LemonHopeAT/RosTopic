/**
 * @file 01_basic_pub_sub.cpp
 * @brief Базовый пример использования подписок и публикаций
 * 
 * Этот пример демонстрирует:
 * - Создание Factory (аналог Node в ROS2)
 * - Создание Publisher для отправки сообщений
 * - Создание Subscription для получения сообщений
 * - Использование Executor для обработки сообщений
 */

#include "../ros/arch/include/communication/impl/executor.h"
#include "../ros/arch/include/experimental/test_message.h"
#include <arch/communication/imessage.h>
#include <communication/impl/factory.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

using namespace arch::experimental;

// Простая структура данных для сообщения
struct MyData
{
    int value;
    std::string text;

    MyData() : value(0), text("") {}
    MyData(int v, const std::string& t) : value(v), text(t) {}
};

int main()
{
    std::cout << "=== Базовый пример подписки и публикации ===\n\n";

    // 1. Создаем Factory (аналог Node в ROS2)
    auto factory = std::make_shared<Factory>("example_node");

    // 2. Создаем Executor для обработки сообщений
    auto executor = std::make_shared<Executor>(1);    // 1 поток для обработки

    // 3. Создаем подписку (Subscription)
    // Подписка будет получать сообщения типа TestMessage<MyData>
    std::atomic<int> received_count{0};

    auto subscription = factory->createSubscription<TestMessage<MyData>>(
        "/example/topic",    // Имя топика
        [&received_count](arch::message_ptr<const arch::IMessage> msg) {
            // Callback функция вызывается при получении сообщения
            if (msg)
            {
                const auto* my_msg = static_cast<const TestMessage<MyData>*>(msg.get());
                if (my_msg)
                {
                    received_count++;
                    std::cout << "Получено сообщение #" << received_count.load()
                              << ": value=" << my_msg->data.value
                              << ", text=\"" << my_msg->data.text << "\"\n";
                }
            }
        },
        QoS::Reliable(100),    // QoS: надежная доставка, очередь на 100 сообщений
        nullptr,               // CallbackGroup (nullptr = по умолчанию)
        ""                     // Consumer group (пустая строка = Broadcast)
    );

    if (!subscription)
    {
        std::cerr << "Ошибка: не удалось создать подписку\n";
        return 1;
    }

    // 4. Получаем Topic и добавляем его в Executor
    auto topic = factory->getTopic<TestMessage<MyData>>("/example/topic");
    if (topic)
    {
        executor->add_topic(topic);
    }

    // 5. Запускаем Executor
    executor->start();

    // 6. Создаем Publisher
    auto publisher = factory->createPublisher<TestMessage<MyData>>(
        "/example/topic",
        QoS::Reliable(100)    // QoS должен быть совместим с подпиской
    );

    if (!publisher)
    {
        std::cerr << "Ошибка: не удалось создать publisher\n";
        return 1;
    }

    // 7. Отправляем несколько сообщений
    std::cout << "Отправка сообщений...\n";
    for (int i = 1; i <= 5; ++i)
    {
        MyData data(i * 10, "Сообщение #" + std::to_string(i));
        TestMessage<MyData> msg(data);

        if (publisher->publish(msg))
        {
            std::cout << "Отправлено сообщение #" << i << "\n";
        }
        else
        {
            std::cerr << "Ошибка отправки сообщения #" << i << "\n";
        }

        // Небольшая задержка для демонстрации
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 8. Обрабатываем сообщения через Executor
    std::cout << "\nОбработка сообщений...\n";
    for (int i = 0; i < 10; ++i)
    {
        executor->spin_some();    // Обрабатываем до 200 сообщений за раз
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // 9. Ожидаем обработки всех сообщений
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::cout << "\nВсего получено сообщений: " << received_count.load() << "\n";

    // 10. Останавливаем Executor и уничтожаем Factory
    executor->stop();
    factory->destroy();

    std::cout << "\nПример завершен успешно!\n";
    return 0;
}
