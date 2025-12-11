/**
 * @file qos.h
 * @brief Quality of Service settings for topics and subscribers
 * @date 2025
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
        
        /**
         * @brief Check if two QoS profiles are compatible (ROS2-like)
         * @param other Other QoS profile to compare with
         * @return true if compatible, false otherwise
         * @note In ROS2, Reliable publisher requires Reliable subscriber, BestEffort can work with both
         */
        bool is_compatible_with(const QoS& other) const
        {
            // Reliable publisher requires Reliable subscriber
            if (reliability == Reliability::Reliable && other.reliability == Reliability::BestEffort)
            {
                return false;
            }
            // Durability compatibility check (TransientLocal requires TransientLocal)
            if (durability == Durability::TransientLocal && other.durability == Durability::Volatile)
            {
                return false;
            }
            return true;
        }
        
        /**
         * @brief Check if QoS profiles are equal
         * @param other Other QoS profile to compare with
         * @return true if equal, false otherwise
         */
        bool operator==(const QoS& other) const
        {
            return reliability == other.reliability &&
                   durability == other.durability &&
                   delivery == other.delivery &&
                   history_depth == other.history_depth &&
                   deadline == other.deadline;
        }
        
        /**
         * @brief Check if QoS profiles are not equal
         * @param other Other QoS profile to compare with
         * @return true if not equal, false otherwise
         */
        bool operator!=(const QoS& other) const
        {
            return !(*this == other);
        }
    };
}    // namespace arch::experimental

#endif    // !QOS_H
