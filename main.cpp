
#include "arch/communication/imessage.h"
#include "arch/utils.h"
#include <communication/impl/callback_group.h>
#include <communication/impl/executor.h>
#include <communication/impl/publisher.h>
#include <communication/impl/qos.h>
#include <communication/impl/subscriber_slot.h>
#include <communication/impl/topic.h>

#include <future>
#include <iostream>
#include <vector>

//#define TEST_0
//#define TEST_1
//#define TEST_2
//#define TEST_3
#define TEST_BECH

using namespace std::chrono_literals;
using namespace arch::experimental;

//======================================================================================
// Publisher ----> Topic ----> SubscriberSlot (MPSC queue) ----> Executor ----> callback
//======================================================================================

// ==================== Demo Message Types ====================
struct StringData
{
    std::string text;
    std::chrono::system_clock::time_point timestamp;

    StringData() = default;
    explicit StringData(const std::string& t)
    : text(t), timestamp(std::chrono::system_clock::now()) {}

    StringData(const std::string& t, std::chrono::system_clock::time_point ts)
    : text(t), timestamp(ts) {}
};

struct SensorData
{
    float temperature;
    float humidity;
    uint64_t sensor_id;

    SensorData() : temperature(0), humidity(0), sensor_id(0) {}
    SensorData(float temp, float hum, uint64_t id)
    : temperature(temp), humidity(hum), sensor_id(id) {}
};

struct TaskData
{
    int task_id;
    std::string description;
    std::vector<std::string> parameters;

    TaskData(int id, std::string desc, std::vector<std::string> params = {})
    : task_id(id), description(std::move(desc)), parameters(std::move(params)) {}
};
#ifdef TEST_0
// ==================== Demo ====================
int main()
{
    std::cout << "=== ROS2-like Runtime Demo ===\n\n";

    // Create executor with 4 threads
    auto executor = std::make_shared<Executor>(4);
    executor->start();

    // Create callback groups
    auto cg_fast = std::make_shared<CallbackGroup>(
        CallbackGroup::Type::Reentrant, "fast");

    auto cg_slow = std::make_shared<CallbackGroup>(
        CallbackGroup::Type::MutuallyExclusive, "slow");

    // Create topics with different Qos
    auto chat_topic = std::make_shared<Topic<StringData>>(
        "/chat", QoS::BestEffort(100));

    auto sensor_topic = std::make_shared<Topic<SensorData>>(
        "/sensors", QoS::Reliable(50));

    auto task_topic = std::make_shared<Topic<TaskData>>(
        "/tasks", QoS::SingleConsumer(1000));

    // Register topics with executor
    executor->add_topic(chat_topic);
    executor->add_topic(sensor_topic);
    executor->add_topic(task_topic);

    // Create publishers
    Publisher<StringData> chat_pub(chat_topic);
    Publisher<SensorData> sensor_pub(sensor_topic);
    Publisher<TaskData> task_pub(task_topic);

    std::cout << "Subscribers created\n";

    // Create subscribers for chat (broadcast)
    auto chat_sub1 = chat_topic->subscribe(
        [](auto msg) {
            auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                          msg->data.timestamp.time_since_epoch())
                          .count();
            std::cout << "[Chat Sub1] " << msg->data.text
                      << " (t=" << ts << "ms)\n";
        },
        cg_fast,
        QoS::BestEffort(50));

    auto chat_sub2 = chat_topic->subscribe(
        [](auto msg) {
            std::cout << "[Chat Sub2] " << msg->data.text << "\n";
        },
        cg_fast,
        QoS::BestEffort(50));

    // Create subscribers for sensors
    auto sensor_sub = sensor_topic->subscribe(
        [](auto msg) {
            std::cout << "[Sensor] ID=" << msg->data.sensor_id
                      << " Temp=" << msg->data.temperature
                      << "C Hum=" << msg->data.humidity << "%\n";
        },
        cg_fast,
        QoS::Reliable(10));

    // Create subscribers for tasks (single-consumer)
    auto task_sub1 = task_topic->subscribe(
        [](auto msg) {
            std::cout << "[Worker1] Processing task #" << msg->data.task_id
                      << ": " << msg->data.description << "\n";
            arch::sleep_for(50ms);
        },
        cg_slow,
        QoS::SingleConsumer(100),
        "worker_pool"    // Same consumer group
    );

    auto task_sub2 = task_topic->subscribe(
        [](auto msg) {
            std::cout << "[Worker2] Processing task #" << msg->data.task_id
                      << ": " << msg->data.description << "\n";
            arch::sleep_for(50ms);
        },
        cg_slow,
        QoS::SingleConsumer(100),
        "worker_pool"    // Same consumer group
    );

    auto task_sub3 = task_topic->subscribe(
        [](auto msg) {
            std::cout << "[Worker3] Processing task #" << msg->data.task_id
                      << ": " << msg->data.description << "\n";
            arch::sleep_for(50ms);
        },
        cg_slow,
        QoS::SingleConsumer(100),
        "worker_pool"    // Same consumer group
    );

    // Create a separate consumer group for monitoring
    auto monitor_sub = task_topic->subscribe(
        [](auto msg) {
            std::cout << "[Monitor] Task queued: #" << msg->data.task_id
                      << " - " << msg->data.description << "\n";
        },
        cg_fast,
        QoS::SingleConsumer(10),
        "monitors"    // Different consumer group
    );

    // Give system time to initialize
    arch::sleep_for(100ms);

    // Publish some chat messages (broadcast)
    std::cout << "\n--- Broadcasting chat messages ---\n";
    for (int i = 0; i < 5; ++i)
    {
        chat_pub.notify(StringData("Hello everyone #" + std::to_string(i)));
        arch::sleep_for(10ms);
    }

    // Publish sensor data (reliable)
    std::cout << "\n--- Publishing sensor data ---\n";
    sensor_pub.notify(SensorData{25.5f, 60.0f, 1});
    sensor_pub.notify(SensorData{26.0f, 58.5f, 2});

    // Publish tasks (single-consumer per group)
    std::cout << "\n--- Publishing tasks (single-consumer) ---\n";
    std::cout << "Each task goes to only one worker, but monitor sees all\n\n";

    std::vector<std::future<void>> task_futures;
    for (int i = 0; i < 10; ++i)
    {
        task_futures.push_back(std::async(std::launch::async, [&task_pub, i]() {
            task_pub.notify(TaskData{i + 1, "Process image batch " + std::to_string(i + 1)});
            arch::sleep_for(5ms);
        }));
    }

    // Wait for tasks to be queued
    for (auto& f : task_futures)
    {
        f.wait();
    }

    // Let executor process for a while
    std::cout << "\n--- Processing queued messages ---\n";
    auto start = std::chrono::steady_clock::now();

    // Process messages for 2 seconds
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(2))
    {
        executor->spin_once(100ms);

        // Publish more chat messages dynamically
        static int counter = 0;
        if (counter++ % 5 == 0)
        {
            chat_pub.notify(StringData("Periodic update #" + std::to_string(counter / 5)));
        }

        // Publish occasional sensor updates
        if (counter % 7 == 0)
        {
            static float temp = 25.0f;
            temp += 0.1f;
            sensor_pub.notify(SensorData{temp, 55.0f + (counter % 10), 3});
        }
    }

    // Show statistics
    std::cout << "\n--- Statistics ---\n";
    std::cout << "Chat queue sizes: "
              << chat_sub1->queue_size() << ", "
              << chat_sub2->queue_size() << "\n";
    std::cout << "Sensor queue size: " << sensor_sub->queue_size() << "\n";
    std::cout << "Worker queue sizes: "
              << task_sub1->queue_size() << ", "
              << task_sub2->queue_size() << ", "
              << task_sub3->queue_size() << "\n";
    std::cout << "Monitor queue size: " << monitor_sub->queue_size() << "\n";

    // Publish some final tasks
    std::cout << "\n--- Final tasks ---\n";
    for (int i = 10; i < 15; ++i)
    {
        task_pub.notify(TaskData{i + 1, "Final task " + std::to_string(i + 1)});
        arch::sleep_for(20ms);
    }

    // Process remaining messages
    std::cout << "\n--- Processing remaining messages ---\n";
    executor->spin_all(500);

    // Cleanup
    std::cout << "\n--- Cleaning up ---\n";

    // Unsubscribe manually (optional - happens automatically on destruction)
    chat_topic->unsubscribe(chat_sub1);
    chat_topic->unsubscribe(chat_sub2);
    sensor_topic->unsubscribe(sensor_sub);
    task_topic->unsubscribe(task_sub1);
    task_topic->unsubscribe(task_sub2);
    task_topic->unsubscribe(task_sub3);
    task_topic->unsubscribe(monitor_sub);

    executor->stop();

    std::cout << "\n=== Demo completed successfully ===\n";
    return 0;
}
#endif

#ifdef TEST_1
int main()
{
    // Создание исполнителя
    auto executor = std::make_shared<arch::Executor>(4);
    executor->start();

    // Создание групп обратных вызовов
    auto cg_fast = std::make_shared<arch::CallbackGroup>(
        arch::CallbackGroup::Type::Reentrant, "fast");

    // Создание топика
    auto chat_topic = std::make_shared<arch::Topic<StringData>>(
        "/chat", arch::QoS::BestEffort(100));

    // Регистрация топика в исполнителе
    executor->add_topic(chat_topic);

    // Создание публикатора
    arch::Publisher<StringData> chat_pub(chat_topic);

    // Подписка на топик
    auto chat_sub = chat_topic->subscribe(
        [](auto msg) {
            std::cout << "Received: " << msg->data.text << "\n";
        },
        cg_fast,
        arch::QoS::BestEffort(50));

    // Публикация сообщения
    chat_pub.publish(StringData("Hello World"));

    // Обработка сообщений
    executor->spin_some();

    // Очистка
    executor->stop();

    return 0;
}
#endif

#ifdef TEST_2
struct ChatMessage
{
    std::string sender;
    std::string content;
    std::chrono::system_clock::time_point timestamp;
    int priority = 0;    // 0 - normal, 1 - important, 2 - urgent

    ChatMessage() = default;

    ChatMessage(const std::string& snd, const std::string& cnt, int prio = 0)
    : sender(snd), content(cnt), priority(prio),
      timestamp(std::chrono::system_clock::now()) {}

    ChatMessage(const std::string& snd, const std::string& cnt,
                std::chrono::system_clock::time_point ts, int prio = 0)
    : sender(snd), content(cnt), timestamp(ts), priority(prio) {}

    std::string toString() const
    {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      timestamp.time_since_epoch())
                      .count();
        return "[" + sender + " @ " + std::to_string(ms) + "ms] " + content +
               " (priority: " + std::to_string(priority) + ")";
    }
};

class User
{
private:
    std::string name_;
    std::shared_ptr<arch::Topic<ChatMessage>> chat_topic_;
    std::shared_ptr<arch::CallbackGroup> callback_group_;
    arch::Publisher<ChatMessage> publisher_;
    std::shared_ptr<arch::SubscriberSlot<ChatMessage>> subscription_;

    // Обработчик входящих сообщений
    void handleMessage(message_ptr msg)
    {
        if (!msg)
            return;

        // Получаем конкретный тип сообщения
        auto chat_msg = std::dynamic_pointer_cast<const Message<ChatMessage>>(msg);
        if (!chat_msg)
            return;

        // Игнорируем собственные сообщения (чтобы не было эха)
        if (chat_msg->data.sender == name_)
        {
            return;
        }

        std::cout << "\033[32m[" << name_ << " received]: "
                  << chat_msg->data.toString() << "\033[0m\n";
    }

public:
    User(const std::string& name,
         std::shared_ptr<arch::Topic<ChatMessage>> chat_topic,
         std::shared_ptr<arch::CallbackGroup> callback_group)
    : name_(name),
      chat_topic_(chat_topic),
      callback_group_(callback_group),
      publisher_(chat_topic)
    {
        // Подписываемся на топик
        subscription_ = chat_topic_->subscribe(
            [this](auto msg) { this->handleMessage(msg); },
            callback_group_,
            arch::QoS::BestEffort(50),
            name_    // consumer group = имени пользователя
        );

        std::cout << "user create " << name_ << "\n";
    }

    ~User()
    {
        if (chat_topic_ && subscription_)
        {
            chat_topic_->unsubscribe(subscription_);
        }

        std::cout << "user destroy " << name_ << "\n";
    }

    // Отправка сообщения
    void sendMessage(const std::string& content, int priority = 0)
    {
        ChatMessage msg(name_, content, priority);

        if (publisher_.publish(msg))
        {
            std::cout << "\033[34m[" << name_ << " sent]: "
                      << msg.toString() << "\033[0m\n";
        }
        else
        {
            std::cout << "\033[31m[" << name_ << " failed to send]: "
                      << msg.toString() << "\033[0m\n";
        }
    }

    // Отправка срочного сообщения
    void sendUrgentMessage(const std::string& content)
    {
        sendMessage("[СРОЧНО] " + content, 2);
    }

    const std::string& getName() const { return name_; }
};

int main()
{
    // 1. Создаем исполнитель (пул потоков)
    auto executor = std::make_shared<arch::Executor>(2);    // 2 рабочих потока
    executor->start();

    // 2. Создаем группы обратных вызовов
    auto cg_chat = std::make_shared<arch::CallbackGroup>(
        arch::CallbackGroup::Type::Reentrant, "chat_group");

    // 3. Создаем топик для чата
    auto chat_topic = std::make_shared<arch::Topic<ChatMessage>>(
        "/chat/messages", arch::QoS::Reliable(100));

    // 4. Регистрируем топик в исполнителе
    executor->add_topic(chat_topic);

    // 5. Создаем пользователей (классы, которые будут обмениваться сообщениями)
    std::cout << "\n--- Create users ---\n";
    auto user_1 = std::make_shared<User>("User1", chat_topic, cg_chat);
    auto user_2 = std::make_shared<User>("User2", chat_topic, cg_chat);

    // Даем время на инициализацию
    arch::sleep_for(100ms);

    std::cout << "\n--- Начало обмена сообщениями ---\n";

    // 6. Пользователи отправляют сообщения
    std::vector<std::thread> threads;

    // user_1 отправляет несколько сообщений
    threads.emplace_back([user_1]() {
        arch::sleep_for(50ms);
        user_1->sendMessage("Hi");

        arch::sleep_for(200ms);
        user_1->sendMessage("ho ho ho");

        arch::sleep_for(300ms);
        user_1->sendMessage("ZovZovZov");
    });

    // user_2 отвечает
    threads.emplace_back([user_2]() {
        arch::sleep_for(100ms);
        user_2->sendMessage("user2 sending");

        arch::sleep_for(150ms);
        user_2->sendMessage("Pupupu");

        arch::sleep_for(400ms);
        user_2->sendMessage("Ogogog");
    });

    // Главный поток тоже может отправлять сообщения
    arch::sleep_for(500ms);

    // Создаем временного пользователя из main
    auto main_user = std::make_unique<User>("System", chat_topic, cg_chat);
    main_user->sendMessage("ALLLLLLLLLLLLLLLLLL");
    main_user->sendUrgentMessage("SystemPUPUPUSystem");

    // Ждем завершения всех потоков
    for (auto& thread : threads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }

    // 7. Даем время на обработку всех сообщений
    std::cout << "\n--- Completing message processing ---\n";

    // Запускаем обработку оставшихся сообщений
    for (int i = 0; i < 10; ++i)
    {
        executor->spin_once(100ms);
        arch::sleep_for(50ms);
    }

    // 8. Отправляем прощальные сообщения
    std::cout << "\n--- Completing ---\n";
    user_1->sendMessage("User1 close");
    user_2->sendMessage("user2 close");

    // Обрабатываем последние сообщения
    executor->spin_all(100);

    // 9. Статистика
    std::cout << "\n--- Statiscits ---\n";

    // Получаем слоты топика
    auto slots = chat_topic->get_slots();
    std::cout << "active subscibers: " << slots.size() << "\n";

    for (const auto& slot : slots)
    {
        std::cout << "  Group: " << slot->consumer_group()
                  << ", message from queue: " << slot->queue_size() << "\n";
    }

    // 10. Очистка

    // Удаляем пользователей (автоматически отписываются в деструкторах)
    main_user.reset();    // Удаляем системного пользователя

    // Ждем немного перед остановкой исполнителя
    arch::sleep_for(200ms);

    // Останавливаем исполнитель
    executor->stop();

    return 0;
}
#endif

#ifdef TEST_3

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

using namespace arch;

// Простой CallbackGroup для тестирования
class TestCallbackGroup : public CallbackGroup
{
public:
    TestCallbackGroup(CallbackGroup::Type type = CallbackGroup::Type::MutuallyExclusive) : CallbackGroup(type, "test_group") {}

    void enter()
    {
        active_callbacks.fetch_add(1, std::memory_order_relaxed);
    }

    void leave()
    {
        active_callbacks.fetch_sub(1, std::memory_order_relaxed);
    }

    int get_active_count() const
    {
        return active_callbacks.load(std::memory_order_relaxed);
    }

private:
    std::atomic<int> active_callbacks{0};
};

// Тип сообщения для тестирования
struct TestMessage
{
    int id;
    std::string data;
    std::chrono::steady_clock::time_point timestamp;
};

// Глобальные счетчики для проверки корректности
std::atomic<int64_t> total_messages_sent{0};
std::atomic<int64_t> total_messages_processed{0};
std::atomic<int64_t> callback_exceptions{0};
std::atomic<int64_t> queue_overflows{0};

// Функция для создания QoS
QoS create_qos(bool reliable = true, size_t depth = 1000)
{
    QoS qos;
    qos.reliability   = reliable ? QoS::Reliability::Reliable : QoS::Reliability::BestEffort;
    qos.history_depth = depth;
    qos.durability    = QoS::Durability::Volatile;
    return qos;
}

// Барьер для C++17 (упрощенная реализация)
class SimpleBarrier
{
public:
    explicit SimpleBarrier(size_t count) : count_(count), generation_(0), waiting_(0) {}

    void arrive_and_wait()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        size_t gen = generation_;

        if (++waiting_ == count_)
        {
            generation_++;
            waiting_ = 0;
            cond_.notify_all();
        }
        else
        {
            cond_.wait(lock, [this, gen] { return gen != generation_; });
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable cond_;
    size_t count_;
    size_t generation_;
    size_t waiting_;
};

// Latch для C++17 (упрощенная реализация)
class SimpleLatch
{
public:
    explicit SimpleLatch(size_t count) : count_(count) {}

    void count_down()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (count_ > 0)
        {
            --count_;
            if (count_ == 0)
            {
                cond_.notify_all();
            }
        }
    }

    void wait()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] { return count_ == 0; });
    }

private:
    std::mutex mutex_;
    std::condition_variable cond_;
    size_t count_;
};

// Тест 1: Один продюсер, один консьюмер
void test_single_producer_single_consumer()
{
    std::cout << "=== Test 1: Single Producer Single Consumer ===" << std::endl;

    auto group = std::make_shared<TestCallbackGroup>(CallbackGroup::Type::Reentrant);
    QoS qos    = create_qos(true, 100);

    // Создаем слот
    auto slot = std::make_shared<SubscriberSlot<TestMessage>>(
        [](message_ptr msg) {
            if (!msg)
                return;

            // Получаем конкретный тип сообщения
            auto test_msg = std::dynamic_pointer_cast<const Message<TestMessage>>(msg);
            if (!test_msg)
                return;

            // Имитация обработки
            std::this_thread::sleep_for(std::chrono::microseconds(10));

            total_messages_processed.fetch_add(1, std::memory_order_relaxed);

            // Иногда бросаем исключение для тестирования
            static std::atomic<int> exception_counter{0};
            if (exception_counter.fetch_add(1) % 100 == 0)
            {
                callback_exceptions.fetch_add(1, std::memory_order_relaxed);
                throw std::runtime_error("Simulated callback exception");
            }
        },
        group,
        qos,
        "test_group");

    constexpr int NUM_MESSAGES = 10000;
    std::atomic<bool> stop_flag{false};

    // Поток продюсера
    std::thread producer([&]() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(1, 100);

        for (int i = 0; i < NUM_MESSAGES; ++i)
        {
            TestMessage test_data;
            test_data.id        = i;
            test_data.data      = "Message_" + std::to_string(i);
            test_data.timestamp = std::chrono::steady_clock::now();
            auto msg            = makeMessage<TestMessage>(std::move(test_data));

            if (!slot->push_message(std::move(msg)))
            {
                queue_overflows.fetch_add(1, std::memory_order_relaxed);
                // При переполнении ждем
                std::this_thread::sleep_for(std::chrono::microseconds(dis(gen)));
            }
            else
            {
                total_messages_sent.fetch_add(1, std::memory_order_relaxed);
            }

            // Рандомная задержка между сообщениями
            std::this_thread::sleep_for(std::chrono::microseconds(dis(gen)));
        }

        // Даем время на обработку оставшихся сообщений
        std::this_thread::sleep_for(std::chrono::seconds(1));
        stop_flag.store(true, std::memory_order_release);
    });

    // Поток консьюмера
    std::thread consumer([&]() {
        while (!stop_flag.load(std::memory_order_acquire) || slot->has_messages())
        {
            if (auto msg = slot->pop_message())
            {
                slot->execute_callback(std::move(*msg));
            }
            else
            {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    std::cout << "Sent: " << total_messages_sent.load()
              << ", Processed: " << total_messages_processed.load()
              << ", Overflows: " << queue_overflows.load()
              << ", Exceptions: " << callback_exceptions.load() << std::endl;
}

// Тест 2: Множество продюсеров, один консьюмер
void test_multi_producer_single_consumer()
{
    std::cout << "\n=== Test 2: Multi Producer Single Consumer ===" << std::endl;

    auto group = std::make_shared<TestCallbackGroup>();
    QoS qos    = create_qos(true, 500);

    auto slot = std::make_shared<SubscriberSlot<TestMessage>>(
        [](message_ptr msg) {
            if (!msg)
                return;
            total_messages_processed.fetch_add(1, std::memory_order_relaxed);
        },
        group,
        qos);

    constexpr int NUM_PRODUCERS         = 5;
    constexpr int MESSAGES_PER_PRODUCER = 2000;

    std::atomic<int> producers_done{0};
    std::atomic<bool> stop_flag{false};

    std::vector<std::thread> producers;

    // Создаем продюсеров - БЕЗ БАРЬЕРА
    for (int p = 0; p < NUM_PRODUCERS; ++p)
    {
        producers.emplace_back([&, p]() {
            for (int i = 0; i < MESSAGES_PER_PRODUCER; ++i)
            {
                TestMessage test_data;
                test_data.id   = p * 10000 + i;
                test_data.data = "Producer_" + std::to_string(p) + "_Msg_" + std::to_string(i);
                auto msg       = makeMessage<TestMessage>(std::move(test_data));

                if (!slot->push_message(std::move(msg)))
                {
                    queue_overflows.fetch_add(1, std::memory_order_relaxed);
                    std::this_thread::yield();
                }
                else
                {
                    total_messages_sent.fetch_add(1, std::memory_order_relaxed);
                }

                // Небольшая задержка для fairness
                if (i % 100 == 0)
                {
                    std::this_thread::yield();
                }
            }

            producers_done.fetch_add(1, std::memory_order_release);
        });
    }

    // Поток консьюмера
    std::thread consumer([&]() {
        int empty_cycles               = 0;
        constexpr int MAX_EMPTY_CYCLES = 1000;

        while (true)
        {
            if (auto msg = slot->pop_message())
            {
                slot->execute_callback(std::move(*msg));
                empty_cycles = 0;
            }
            else
            {
                empty_cycles++;
                // Проверяем, все ли продюсеры закончили и очередь пуста
                if (producers_done.load(std::memory_order_acquire) == NUM_PRODUCERS &&
                    slot->queue_size() == 0)
                {
                    break;    // Все сообщения обработаны
                }

                if (empty_cycles >= MAX_EMPTY_CYCLES)
                {
                    // Долго нет сообщений, выходим
                    break;
                }
                std::this_thread::yield();
            }
        }

        stop_flag.store(true, std::memory_order_release);
    });

    // Ждем завершения всех продюсеров
    for (auto& t : producers)
    {
        t.join();
    }

    std::cout << "All producers finished. Waiting for consumer..." << std::endl;

    // Ждем завершения потребителя
    consumer.join();

    std::cout << "Producers: " << NUM_PRODUCERS
              << ", Sent: " << total_messages_sent.load()
              << ", Processed: " << total_messages_processed.load()
              << ", Overflows: " << queue_overflows.load() << std::endl;
}

// Тест 3: Тестирование уничтожения во время работы
void test_destruction_during_operation()
{
    std::cout << "\n=== Test 3: Destruction During Operation ===" << std::endl;

    constexpr int NUM_SLOTS = 10;
    std::vector<std::shared_ptr<SubscriberSlot<int>>> slots;
    std::atomic<int> messages_processed{0};

    // Создаем несколько слотов
    for (int i = 0; i < NUM_SLOTS; ++i)
    {
        auto slot = std::make_shared<SubscriberSlot<int>>(
            [&messages_processed](message_ptr msg) {
                if (msg)
                {
                    messages_processed.fetch_add(1, std::memory_order_relaxed);
                    // Долгая обработка
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            },
            nullptr,
            create_qos(true, 100));
        slots.push_back(slot);
    }

    std::atomic<bool> stop_flag{false};
    std::vector<std::thread> workers;

    // Рабочие потоки, которые обрабатывают сообщения
    for (int i = 0; i < 4; ++i)
    {
        workers.emplace_back([&, i]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> slot_dist(0, NUM_SLOTS - 1);

            while (!stop_flag.load(std::memory_order_acquire))
            {
                int slot_idx = slot_dist(gen);
                if (auto msg = slots[slot_idx]->pop_message())
                {
                    slots[slot_idx]->execute_callback(std::move(*msg));
                }
                std::this_thread::yield();
            }
        });
    }

    // Поток, который отправляет сообщения
    std::thread sender([&]() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> slot_dist(0, NUM_SLOTS - 1);
        std::uniform_int_distribution<> value_dist(0, 1000);

        for (int i = 0; i < 5000; ++i)
        {
            int slot_idx = slot_dist(gen);
            auto msg     = makeMessage<int>(value_dist(gen));

            if (!slots[slot_idx]->push_message(std::move(msg)))
            {
                queue_overflows.fetch_add(1, std::memory_order_relaxed);
            }

            if (i % 1000 == 0)
            {
                // Уничтожаем случайный слот
                int destroy_idx = slot_dist(gen);
                slots[destroy_idx]->destroy();
            }

            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        stop_flag.store(true, std::memory_order_release);
    });

    sender.join();

    for (auto& worker : workers)
    {
        worker.join();
    }

    std::cout << "Processed during destruction: " << messages_processed.load() << std::endl;
}

// Тест 4: Нагрузочный тест с большой очередью
void test_stress_high_throughput()
{
    std::cout << "\n=== Test 4: Stress Test - High Throughput ===" << std::endl;

    auto group = std::make_shared<TestCallbackGroup>();
    QoS qos    = create_qos(false, 10000);    // Best effort, большая очередь

    std::atomic<int64_t> processed{0};
    auto start_time = std::chrono::steady_clock::now();

    auto slot = std::make_shared<SubscriberSlot<int>>(
        [&processed](message_ptr msg) {
            if (msg)
            {
                processed.fetch_add(1, std::memory_order_relaxed);
            }
        },
        group,
        qos);

    constexpr int NUM_PRODUCERS = 8;
    constexpr int NUM_CONSUMERS = 4;
    constexpr int DURATION_MS   = 5000;

    std::atomic<bool> stop_flag{false};
    SimpleLatch producers_start(NUM_PRODUCERS);
    SimpleLatch consumers_start(NUM_CONSUMERS);

    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    // Быстрые продюсеры
    for (int i = 0; i < NUM_PRODUCERS; ++i)
    {
        producers.emplace_back([&, i]() {
            producers_start.count_down();
            producers_start.wait();

            int message_id = i * 1000000;

            while (!stop_flag.load(std::memory_order_acquire))
            {
                auto msg = makeMessage<int>(message_id++);

                // Не проверяем результат - это best effort
                slot->push_message(std::move(msg));
                total_messages_sent.fetch_add(1, std::memory_order_relaxed);

                // Максимальная скорость - небольшая задержка для fairness
                // std::this_thread::yield();
            }
        });
    }

    // Консьюмеры
    for (int i = 0; i < NUM_CONSUMERS; ++i)
    {
        consumers.emplace_back([&]() {
            consumers_start.count_down();
            consumers_start.wait();

            while (!stop_flag.load(std::memory_order_acquire) || slot->has_messages())
            {
                if (auto msg = slot->pop_message())
                {
                    slot->execute_callback(std::move(*msg));
                }
                else
                {
                    std::this_thread::yield();
                }
            }
        });
    }

    // Ждем начала всех потоков
    producers_start.wait();
    consumers_start.wait();

    // Запускаем тест на заданное время
    std::this_thread::sleep_for(std::chrono::milliseconds(DURATION_MS));
    stop_flag.store(true, std::memory_order_release);

    // Даем время на завершение
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Завершаем потоки
    for (auto& p : producers)
        p.join();
    for (auto& c : consumers)
        c.join();

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    double messages_per_sec = (double)processed.load() / (duration.count() / 1000.0);

    std::cout << "Duration: " << duration.count() << "ms" << std::endl;
    std::cout << "Sent: " << total_messages_sent.load() << std::endl;
    std::cout << "Processed: " << processed.load() << std::endl;
    std::cout << "Throughput: " << messages_per_sec << " msg/sec" << std::endl;
    std::cout << "Queue size at end: " << slot->queue_size() << std::endl;
}

// Тест 5: Проверка memory ordering и атомарности
void test_memory_ordering()
{
    std::cout << "\n=== Test 5: Memory Ordering Test ===" << std::endl;

    std::atomic<int> consistency_errors{0};
    std::atomic<int> last_seen_id{-1};

    auto group = std::make_shared<TestCallbackGroup>();
    QoS qos    = create_qos(true, 1000);

    auto slot = std::make_shared<SubscriberSlot<int>>(
        [&](message_ptr msg) {
            if (!msg)
                return;

            // Получаем конкретный тип сообщения
            auto int_msg = std::dynamic_pointer_cast<const Message<int>>(msg);
            if (!int_msg)
                return;

            int current_id = int_msg->data;
            int previous   = last_seen_id.exchange(current_id);

            // Проверяем, что сообщения приходят в порядке возрастания
            // (для одного продюсера)
            if (previous != -1 && current_id <= previous)
            {
                consistency_errors.fetch_add(1, std::memory_order_relaxed);
            }
        },
        group,
        qos);

    constexpr int NUM_MESSAGES = 100000;
    std::atomic<bool> stop_flag{false};

    std::thread producer([&]() {
        for (int i = 0; i < NUM_MESSAGES; ++i)
        {
            auto msg = makeMessage<int>(i);
            slot->push_message(std::move(msg));
        }
        stop_flag.store(true, std::memory_order_release);
    });

    std::thread consumer([&]() {
        while (!stop_flag.load(std::memory_order_acquire) || slot->has_messages())
        {
            if (auto msg = slot->pop_message())
            {
                slot->execute_callback(std::move(*msg));
            }
            else
            {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    std::cout << "Total messages: " << NUM_MESSAGES << std::endl;
    std::cout << "Consistency errors: " << consistency_errors.load() << std::endl;
    std::cout << "Last processed ID: " << last_seen_id.load() << std::endl;
}

// Дополнительный тест: проверка Consumer Group
void test_consumer_groups()
{
    std::cout << "\n=== Test 6: Consumer Groups ===" << std::endl;

    std::atomic<int> group1_processed{0};
    std::atomic<int> group2_processed{0};

    auto group = std::make_shared<TestCallbackGroup>();
    QoS qos    = create_qos(true, 100);

    // Создаем два слота с разными consumer groups
    auto slot1 = std::make_shared<SubscriberSlot<int>>(
        [&group1_processed](message_ptr msg) {
            if (msg)
            {
                group1_processed.fetch_add(1, std::memory_order_relaxed);
            }
        },
        group,
        qos,
        "group1");

    auto slot2 = std::make_shared<SubscriberSlot<int>>(
        [&group2_processed](message_ptr msg) {
            if (msg)
            {
                group2_processed.fetch_add(1, std::memory_order_relaxed);
            }
        },
        group,
        qos,
        "group2");

    constexpr int NUM_MESSAGES = 5000;
    std::atomic<bool> stop_flag{false};

    // Отправляем сообщения в оба слота
    std::thread producer([&]() {
        for (int i = 0; i < NUM_MESSAGES; ++i)
        {
            auto msg1 = makeMessage<int>(i);
            auto msg2 = makeMessage<int>(i * 2);

            slot1->push_message(std::move(msg1));
            slot2->push_message(std::move(msg2));

            total_messages_sent.fetch_add(2, std::memory_order_relaxed);

            if (i % 1000 == 0)
            {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
        stop_flag.store(true, std::memory_order_release);
    });

    // Обрабатываем сообщения из обоих слотов
    std::thread consumer([&]() {
        while (!stop_flag.load(std::memory_order_acquire) ||
               slot1->has_messages() || slot2->has_messages())
        {

            // Пробуем обработать из первого слота
            if (auto msg = slot1->pop_message())
            {
                slot1->execute_callback(std::move(*msg));
            }

            // Пробуем обработать из второго слота
            if (auto msg = slot2->pop_message())
            {
                slot2->execute_callback(std::move(*msg));
            }

            std::this_thread::yield();
        }
    });

    producer.join();
    consumer.join();

    std::cout << "Group1 processed: " << group1_processed.load() << std::endl;
    std::cout << "Group2 processed: " << group2_processed.load() << std::endl;
    std::cout << "Total sent: " << total_messages_sent.load() << std::endl;
}

int main()
{
    for (size_t i = 0; i < 100000; ++i)
    {
        std::cout << "Try #: " << i << std::endl;

        try
        {
            // Сброс счетчиков перед запуском
            total_messages_sent      = 0;
            total_messages_processed = 0;
            callback_exceptions      = 0;
            queue_overflows          = 0;

            // Запуск тестов
            test_single_producer_single_consumer();

            // Сброс счетчиков между тестами
            total_messages_sent      = 0;
            total_messages_processed = 0;
            queue_overflows          = 0;

            test_multi_producer_single_consumer();

            total_messages_sent      = 0;
            total_messages_processed = 0;
            queue_overflows          = 0;

            test_destruction_during_operation();

            total_messages_sent      = 0;
            total_messages_processed = 0;
            queue_overflows          = 0;

            test_stress_high_throughput();

            total_messages_sent      = 0;
            total_messages_processed = 0;

            test_memory_ordering();

            total_messages_sent      = 0;
            total_messages_processed = 0;

            test_consumer_groups();

            std::cout << "\n=== All tests completed ===" << std::endl;
        }
        catch (const std::exception& e)
        {
            std::cerr << "Test failed with exception: " << e.what() << std::endl;
            return 1;
        }
    }
    return 0;
}

#endif

#ifdef TEST_BECH
#include <experimental/benchmark.h>
int main()
{
    try
    {
        std::cout << "Запуск бенчмарков системы сообщений...\n";
        std::cout << "Система: "
                  <<
#ifdef __linux__
            "Linux"
#elif _WIN32
            "Windows"
#elif __APPLE__
            "macOS"
#else
            "Unknown"
#endif
                  << "\n";

        std::cout << "Компилятор: "
                  <<
#ifdef __clang__
            "Clang " << __clang_major__ << "." << __clang_minor__
#elif __GNUC__
            "GCC " << __GNUC__ << "." << __GNUC_MINOR__
#elif _MSC_VER
            "MSVC " << _MSC_VER
#else
            "Unknown"
#endif
                  << "\n";

        std::cout << "Размер указателя: " << sizeof(void*) * 8 << "-bit\n";

        std::vector<Benchmark::BenchmarkResult> all_results;

        // Запускаем бенчмарки в цикле с усреднением
        // Каждый тест запускается несколько раз и усредняется
        const size_t iterations_per_test = 3;      // Количество итераций для усреднения каждого теста
        const size_t total_cycles        = 100;    // Общее количество циклов

        for (size_t i = 0; i < total_cycles; ++i)
        {
            std::cout << "\n========================================\n";
            std::cout << "Cycle #: " << i << " / " << total_cycles << "\n";
            std::cout << "========================================\n";

            // run_all_benchmarks теперь принимает параметр iterations_per_test
            // и усредняет результаты внутри
            auto results = Benchmark::run_all_benchmarks(iterations_per_test);
            all_results.insert(all_results.end(), results.begin(), results.end());

            // Сохраняем промежуточные результаты каждые 10 циклов
            if ((i + 1) % 10 == 0)
            {
                std::string filename = "benchmark_summary_cycle_" + std::to_string(i + 1) + ".csv";
                Benchmark::save_results_to_csv(all_results, filename);
                std::cout << "\nIntermediate results saved to: " << filename << "\n";
            }
        }

        // Суммарный вывод и CSV
        Benchmark::save_results_to_csv(all_results, "benchmark_summary.csv");

        std::cout << "\n=========================================\n";
        std::cout << "   БЕНЧМАРКИ УСПЕШНО ЗАВЕРШЕНЫ          \n";
        std::cout << "=========================================\n";

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Ошибка при выполнении бенчмарков: " << e.what() << std::endl;
        return 1;
    }
    catch (...)
    {
        std::cerr << "Неизвестная ошибка при выполнении бенчмарков" << std::endl;
        return 1;
    }
}
#endif
