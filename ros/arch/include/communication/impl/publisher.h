/**
 * @file publisher.h
 * @brief Publisher for publishing messages to topics
 * @date 2025
 * @version 1.0.0
 * @ingroup arch_experimental
 */

#ifndef ARCH_COMM_A_PUBLISHER_H
#define ARCH_COMM_A_PUBLISHER_H

#include "topic.h"
#include <arch/communication/imessage.h>
#include <memory>

namespace arch::experimental
{

    /**
     * @brief Publisher for publishing messages to topics
     * @ingroup arch_experimental
     * @tparam MessageT Type of message data
     * 
     * Similar to rclcpp::Publisher in ROS2, but uses arch library components.
     */
    template <typename MessageT>
    class Publisher
    {
    public:
        /**
         * @brief Constructs publisher for given topic
         * @param topic Shared pointer to topic
         */
        Publisher(std::shared_ptr<Topic<MessageT>> topic) : topic_(std::move(topic))
        {
            if (topic_)
            {
                topic_->register_publisher();
            }
        }
        
        /**
         * @brief Destructor - unregisters publisher from topic
         */
        ~Publisher()
        {
            if (topic_)
            {
                topic_->unregister_publisher();
            }
        }
        
        Publisher(const Publisher& other) : topic_(other.topic_)
        {
            if (topic_)
            {
                topic_->register_publisher();
            }
        }
        
        Publisher& operator=(const Publisher& other)
        {
            if (this != &other)
            {
                if (topic_)
                {
                    topic_->unregister_publisher();
                }
                topic_ = other.topic_;
                if (topic_)
                {
                    topic_->register_publisher();
                }
            }
            return *this;
        }
        
        Publisher(Publisher&& other) noexcept : topic_(std::move(other.topic_))
        {
            other.topic_.reset();
        }
        
        Publisher& operator=(Publisher&& other) noexcept
        {
            if (this != &other)
            {
                if (topic_)
                {
                    topic_->unregister_publisher();
                }
                topic_ = std::move(other.topic_);
                other.topic_.reset();
            }
            return *this;
        }

        /**
         * @brief Publish message pointer (IMessagePtr) - ROS2-like API
         * @param msg Message pointer to publish
         * @return true if published successfully, false otherwise
         */
        bool publish(message_ptr<const IMessage> msg)
        {
            return topic_ ? topic_->publish(std::move(msg)) : false;
        }
        
        /**
         * @brief Publish message pointer (IMessagePtr) - legacy method
         * @param msg Message pointer to publish
         * @return true if published successfully, false otherwise
         */
        bool notify(message_ptr<const IMessage> msg)
        {
            return publish(std::move(msg));
        }

        /**
         * @brief Publish message data (copied) - ROS2-like API
         * @param data Message data to publish
         * @return true if published successfully, false otherwise
         */
        bool publish(const MessageT& data)
        {
            return publish(arch::makeSharedMsg<MessageT>(data));
        }
        
        /**
         * @brief Publish message data (moved) - ROS2-like API
         * @param data Message data to publish
         * @return true if published successfully, false otherwise
         */
        bool publish(MessageT&& data)
        {
            return publish(arch::makeSharedMsg<MessageT>(std::move(data)));
        }
        
        /**
         * @brief Publish message data (copied) - legacy method
         * @param data Message data to publish
         * @return true if published successfully, false otherwise
         */
        bool notify(const MessageT& data)
        {
            return publish(data);
        }

        /**
         * @brief Publish message data (moved) - legacy method
         * @param data Message data to publish
         * @return true if published successfully, false otherwise
         */
        bool notify(MessageT&& data)
        {
            return publish(std::move(data));
        }

        /**
         * @brief Get topic name
         * @return Topic name string (empty if topic is null)
         */
        const std::string& get_topic_name() const
        {
            return topic_ ? topic_->get_topic_name() : empty_;
        }
        
        /**
         * @brief Get topic name (legacy method for compatibility)
         * @return Topic name string (empty if topic is null)
         */
        const std::string& topic_name() const
        {
            return get_topic_name();
        }
        
        /**
         * @brief Get number of subscriptions to this topic
         * @return Number of active subscriptions
         */
        size_t get_subscription_count() const
        {
            return topic_ ? topic_->get_subscription_count() : 0;
        }
        
        /**
         * @brief Get number of publishers for this topic
         * @return Number of active publishers
         */
        size_t get_publisher_count() const
        {
            return topic_ ? topic_->get_publisher_count() : 0;
        }
        
        /**
         * @brief Check if publisher is valid
         * @return true if topic is valid, false otherwise
         */
        bool is_valid() const
        {
            return topic_ != nullptr;
        }
        
        /**
         * @brief Get underlying topic
         * @return Shared pointer to topic (can be nullptr)
         */
        std::shared_ptr<Topic<MessageT>> get_topic() const
        {
            return topic_;
        }
        
        /**
         * @brief Get QoS settings for this publisher
         * @return Reference to QoS settings
         */
        const QoS& get_qos() const
        {
            static const QoS empty_qos;
            return topic_ ? topic_->get_qos() : empty_qos;
        }

    private:
        std::shared_ptr<Topic<MessageT>> topic_;    ///< Topic to publish to
        static inline std::string empty_;           ///< Empty string for null topic
    };

}    // namespace arch::experimental

#endif    // !ARCH_COMM_A_PUBLISHER_H
