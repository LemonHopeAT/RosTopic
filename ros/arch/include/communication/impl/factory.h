/**
 * @file factory.h
 * @brief Factory class for creating publishers and subscriptions (ROS2-like API)
 * @date 15.12.2025
 * @version 1.0.0
 * @ingroup arch_experimental
 */

#ifndef ARCH_COMM_FACTORY_H
#define ARCH_COMM_FACTORY_H

#include "callback_group.h"
#include "publisher.h"
#include "qos.h"
#include "subscription.h"
#include "topic_registry.h"
#include <arch/communication/imessage.h>
#include <memory>
#include <string>

namespace arch::experimental
{

    /**
     * @brief Factory for creating publishers and subscriptions (ROS2-like API)
     * @ingroup arch_experimental
     *
     * Factory class similar to rclcpp::Node in ROS2. Provides interface for creating publishers,
     * subscriptions, and managing their lifecycle. Acts as a factory for ROS2-like entities.
     */
    class Factory
    {
    public:
        /**
         * @brief Constructs factory
         * @param name Factory name
         */
        explicit Factory(const std::string& name);

        Factory(const Factory&)            = delete;
        Factory& operator=(const Factory&) = delete;

        ~Factory();

        /**
         * @brief Get factory name
         * @return Factory name string
         */
        const std::string& getName() const { return name_; }

        /**
         * @brief Create publisher for topic
         * @tparam Type Type of message data
         * @param topic_name Topic name
         * @param qos Quality of Service settings
         * @return Shared pointer to publisher
         */
        template <typename Type>
        std::shared_ptr<Publisher<Type>> createPublisher(const std::string& topic_name, const QoS& qos = QoS())
        {
            if (destroyed_.load())
                return nullptr;

            auto topic = TopicRegistry::instance().getOrCreateTopic<Type>(topic_name, qos);
            if (!topic)
                return nullptr;

            return std::make_shared<Publisher<Type>>(topic);
        }

        /**
         * @brief Create subscription to topic
         * @tparam Type Type of message data
         * @param topic_name Topic name
         * @param callback Callback function to execute when message is received
         * @param qos Quality of Service settings
         * @param group Callback group for thread synchronization (can be nullptr)
         * @param consumer_group Consumer group identifier (for SingleConsumer delivery)
         * @return Shared pointer to subscription
         */
        template <typename Type>
        std::shared_ptr<Subscription<Type>> createSubscription(
            const std::string& topic_name,
            std::function<void(message_ptr<const IMessage>)> callback,
            const QoS& qos                              = QoS(),
            std::shared_ptr<CallbackGroup> group       = nullptr,
            const std::string& consumer_group          = "")
        {
            if (destroyed_.load())
                return nullptr;

            auto topic = TopicRegistry::instance().getOrCreateTopic<Type>(topic_name, qos);
            if (!topic)
                return nullptr;

            return std::make_shared<Subscription<Type>>(
                topic, std::move(callback), std::move(group), qos, consumer_group);
        }

        /**
         * @brief Create subscription to topic (non-template version, works with IMessagePtr)
         * @param topic_name Topic name
         * @param callback Callback function to execute when message is received
         * @param qos Quality of Service settings
         * @param group Callback group for thread synchronization (can be nullptr)
         * @param consumer_group Consumer group identifier (for SingleConsumer delivery)
         * @return Shared pointer to subscription (type-erased as void*)
         * @note This version uses Topic<IMessage> internally to work with IMessagePtr directly
         */
        std::shared_ptr<void> createSubscription(
            const std::string& topic_name,
            std::function<void(message_ptr<const IMessage>)> callback,
            const QoS& qos                              = QoS(),
            std::shared_ptr<CallbackGroup> group       = nullptr,
            const std::string& consumer_group          = "")
        {
            if (destroyed_.load())
                return nullptr;

            // Use IMessage as the topic type for type-erased subscriptions working with IMessagePtr
            auto topic = TopicRegistry::instance().getOrCreateTopic<IMessage>(topic_name, qos);
            if (!topic)
                return nullptr;

            auto subscription = std::make_shared<Subscription<IMessage>>(
                topic, std::move(callback), std::move(group), qos, consumer_group);
            return std::static_pointer_cast<void>(subscription);
        }

        /**
         * @brief Get topic by name
         * @tparam Type Type of message data
         * @param topic_name Topic name
         * @return Shared pointer to topic (nullptr if not found)
         */
        template <typename Type>
        std::shared_ptr<Topic<Type>> getTopic(const std::string& topic_name) const
        {
            if (destroyed_.load())
                return nullptr;

            return TopicRegistry::instance().getTopic<Type>(topic_name);
        }

        /**
         * @brief Destroy factory
         * @note Factory no longer stores entities, so this only marks factory as destroyed
         */
        void destroy();

        /**
         * @brief Check if factory is valid
         */
        bool isValid() const { return !destroyed_.load(); }

    private:
        std::string name_;
        std::atomic<bool> destroyed_{false};
    };

}    // namespace arch::experimental

#endif    // !ARCH_COMM_FACTORY_H

