/**
 * @file Qos.h
 * @brief Quality of Service settings for topics and subscribers
 * @date 2024
 * @version 1.0.0
 * @ingroup arch_experimental
 */

#ifndef QOS_H
#define QOS_H

#include <chrono>

namespace arch::experimental
{

    /**
     * @brief Quality of Service settings
     * @ingroup arch_experimental
     *
     * Defines reliability, durability, delivery mode, and history depth for topics and subscribers.
     */
    struct QoS
    {
        /**
         * @brief Reliability policy
         */
        enum class Reliability
        {
            BestEffort,    ///< Messages may be dropped if queue is full
            Reliable       ///< Messages are retried if queue is full
        };
        
        /**
         * @brief Durability policy
         */
        enum class Durability
        {
            Volatile,        ///< Messages are not persisted
            TransientLocal   ///< Messages are persisted (not implemented)
        };
        
        /**
         * @brief Delivery policy
         */
        enum class Delivery
        {
            Broadcast,       ///< Message delivered to all subscribers
            SingleConsumer   ///< Message delivered to one subscriber per consumer group
        };

        Reliability reliability = Reliability::BestEffort;    ///< Reliability policy
        Durability durability   = Durability::Volatile;       ///< Durability policy
        Delivery delivery       = Delivery::Broadcast;         ///< Delivery policy
        size_t history_depth    = 10;                        ///< Maximum queue depth
        std::chrono::milliseconds deadline{0};                ///< Deadline timeout (not used)

        /**
         * @brief Create BestEffort QoS
         * @param depth Queue depth (default: 10)
         * @return QoS with BestEffort reliability
         */
        static QoS BestEffort(size_t depth = 10)
        {
            QoS qos;
            qos.reliability   = Reliability::BestEffort;
            qos.history_depth = depth;
            return qos;
        }

        /**
         * @brief Create Reliable QoS
         * @param depth Queue depth (default: 10)
         * @return QoS with Reliable reliability
         */
        static QoS Reliable(size_t depth = 10)
        {
            QoS qos;
            qos.reliability   = Reliability::Reliable;
            qos.history_depth = depth;
            return qos;
        }

        /**
         * @brief Create SingleConsumer QoS
         * @param depth Queue depth (default: 10)
         * @return QoS with SingleConsumer delivery
         */
        static QoS SingleConsumer(size_t depth = 10)
        {
            QoS qos;
            qos.delivery      = Delivery::SingleConsumer;
            qos.history_depth = depth;
            return qos;
        }
    };
}    // namespace arch::experimental

#endif    // QOS_H
