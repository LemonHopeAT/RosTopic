#ifndef QOS_H
#define QOS_H

#include <chrono>
namespace arch
{

    struct QoS
    {
        enum class Reliability
        {
            BestEffort,
            Reliable
        };
        enum class Durability
        {
            Volatile,
            TransientLocal
        };
        enum class Delivery
        {
            Broadcast,
            SingleConsumer
        };

        Reliability reliability = Reliability::BestEffort;
        Durability durability   = Durability::Volatile;
        Delivery delivery       = Delivery::Broadcast;
        size_t history_depth    = 10;
        std::chrono::milliseconds deadline{0};

        static QoS BestEffort(size_t depth = 10)
        {
            QoS qos;
            qos.reliability   = Reliability::BestEffort;
            qos.history_depth = depth;
            return qos;
        }

        static QoS Reliable(size_t depth = 10)
        {
            QoS qos;
            qos.reliability   = Reliability::Reliable;
            qos.history_depth = depth;
            return qos;
        }

        static QoS SingleConsumer(size_t depth = 10)
        {
            QoS qos;
            qos.delivery      = Delivery::SingleConsumer;
            qos.history_depth = depth;
            return qos;
        }
    };
}    // namespace arch

#endif    // QOS_H
