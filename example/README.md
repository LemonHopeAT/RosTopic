# Примеры использования ROS Topic библиотеки

Эта папка содержит примеры использования библиотеки для работы с подписками и публикациями сообщений.

## Структура примеров

### 01_basic_pub_sub.cpp
Базовый пример использования подписок и публикаций:
- Создание Factory (аналог Node в ROS2)
- Создание Publisher для отправки сообщений
- Создание Subscription для получения сообщений
- Использование Executor для обработки сообщений

### 02_multiple_subscribers.cpp
Пример с несколькими подписчиками на один топик:
- Создание нескольких подписок на один топик
- Broadcast режим доставки (сообщение получают все подписчики)
- Использование CallbackGroup для синхронизации

### 03_qos_example.cpp
Пример использования различных настроек QoS:
- BestEffort vs Reliable QoS
- Различные размеры очередей
- Демонстрация различий в поведении

### 04_single_consumer.cpp
Пример использования SingleConsumer режима доставки:
- SingleConsumer режим (round-robin доставка между подписчиками)
- Использование consumer_group для группировки подписчиков
- Различие между Broadcast и SingleConsumer режимами

## Компиляция

```bash
cd example
mkdir build
cd build
cmake ..
make
```

## Запуск примеров

```bash
# Базовый пример
./example_01_basic

# Несколько подписчиков
./example_02_multiple

# Пример QoS
./example_03_qos

# SingleConsumer режим
./example_04_single_consumer
```

## Основные концепции

### Factory
`Factory` - это основной класс для создания publishers и subscriptions (аналог `rclcpp::Node` в ROS2).

```cpp
auto factory = std::make_shared<Factory>("node_name");
```

### Publisher
`Publisher` используется для отправки сообщений в топик.

```cpp
auto publisher = factory->createPublisher<TestMessage<MyData>>(
    "/topic/name",
    QoS::Reliable(100)
);

TestMessage<MyData> msg(data);
publisher->publish(msg);
```

### Subscription
`Subscription` используется для получения сообщений из топика.

```cpp
auto subscription = factory->createSubscription<TestMessage<MyData>>(
    "/topic/name",
    [](message_ptr<const arch::IMessage> msg) {
        // Обработка сообщения
    },
    QoS::Reliable(100)
);
```

### Executor
`Executor` обрабатывает сообщения из подписок.

```cpp
auto executor = std::make_shared<Executor>(1);  // 1 поток
executor->add_topic(topic);
executor->start();
executor->spin_some();  // Обработка сообщений
```

### QoS (Quality of Service)
QoS определяет параметры доставки сообщений:

- **Reliability**: `Reliable` (гарантированная доставка) или `BestEffort` (лучшие усилия)
- **Delivery**: `Broadcast` (всем подписчикам) или `SingleConsumer` (одному подписчику)
- **History Depth**: размер очереди сообщений

```cpp
// Надежная доставка
QoS::Reliable(100)

// Лучшие усилия
QoS::BestEffort(10)

// SingleConsumer режим
QoS::SingleConsumer(50)
```

## Примечания

- Все примеры используют `TestMessage<T>` для обертки данных в сообщения
- В production коде рекомендуется использовать `Message<T>` из `arch/communication/imessage.h`
- Executor должен быть запущен перед отправкой сообщений
- QoS publisher и subscription должны быть совместимы

