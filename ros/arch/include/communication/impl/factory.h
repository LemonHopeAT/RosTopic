/**
 * @file factory.h
 * @brief Factory class for creating publishers and subscriptions (ROS2-like API)
 * @date 2025
 * @version 1.0.0
 * @ingroup arch_experimental
 */

#ifndef ARCH_COMM_FACTORY_H
#define ARCH_COMM_FACTORY_H

#include "callback_group.h"
#include "publisher.h"
#include "qos.h"
#include "subscription.h"
#include "topic.h"
#include <arch/communication/imessage.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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
         * @tparam MessageT Type of message data
         * @param topic_name Topic name
         * @param qos Quality of Service settings
         * @return Shared pointer to publisher
         */
        template <typename MessageT>
        std::shared_ptr<Publisher<MessageT>> createPublisher(const std::string& topic_name, const QoS& qos = QoS())
        {
            if (destroyed_.load())
                return nullptr;

            auto topic = getOrCreateTopic<MessageT>(topic_name, qos);
            if (!topic)
                return nullptr;

            auto publisher = std::make_shared<Publisher<MessageT>>(topic);
            publishers_.push_back(std::static_pointer_cast<void>(publisher));
            return publisher;
        }

        /**
         * @brief Create subscription to topic
         * @tparam MessageT Type of message data
         * @param topic_name Topic name
         * @param callback Callback function to execute when message is received
         * @param qos Quality of Service settings
         * @param group Callback group for thread synchronization (can be nullptr)
         * @param consumer_group Consumer group identifier (for SingleConsumer delivery)
         * @return Shared pointer to subscription
         */
        template <typename MessageT>
        std::shared_ptr<Subscription<MessageT>> createSubscription(
            const std::string& topic_name,
            std::function<void(message_ptr<const IMessage>)> callback,
            const QoS& qos                              = QoS(),
            std::shared_ptr<CallbackGroup> group       = nullptr,
            const std::string& consumer_group          = "")
        {
            if (destroyed_.load())
                return nullptr;

            auto topic = getOrCreateTopic<MessageT>(topic_name, qos);
            if (!topic)
                return nullptr;

            auto subscription = std::make_shared<Subscription<MessageT>>(
                topic, std::move(callback), std::move(group), qos, consumer_group);
            subscriptions_.push_back(std::static_pointer_cast<void>(subscription));
            return subscription;
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
            auto topic = getOrCreateTopic<IMessage>(topic_name, qos);
            if (!topic)
                return nullptr;

            auto subscription = std::make_shared<Subscription<IMessage>>(
                topic, std::move(callback), std::move(group), qos, consumer_group);
            subscriptions_.push_back(std::static_pointer_cast<void>(subscription));
            return std::static_pointer_cast<void>(subscription);
        }

        /**
         * @brief Get topic by name
         * @tparam MessageT Type of message data
         * @param topic_name Topic name
         * @return Shared pointer to topic (nullptr if not found)
         */
        template <typename MessageT>
        std::shared_ptr<Topic<MessageT>> getTopic(const std::string& topic_name) const
        {
            if (destroyed_.load())
                return nullptr;

            std::string key = topic_name + "_" + typeid(MessageT).name();
            auto it        = topics_.find(key);
            if (it != topics_.end())
            {
                return std::static_pointer_cast<Topic<MessageT>>(it->second);
            }
            return nullptr;
        }

        /**
         * @brief Get all subscriptions
         * @return Vector of subscription shared pointers (as void* for type erasure)
         */
        const std::vector<std::shared_ptr<void>>& getSubscriptions() const { return subscriptions_; }

        /**
         * @brief Get all publishers
         * @return Vector of publisher shared pointers (as void* for type erasure)
         */
        const std::vector<std::shared_ptr<void>>& getPublishers() const { return publishers_; }

        /**
         * @brief Get all topics
         * @return Map of topic names to topic shared pointers (as void* for type erasure)
         */
        const std::unordered_map<std::string, std::shared_ptr<void>>& getTopics() const { return topics_; }

        /**
         * @brief Destroy factory and all its entities
         */
        void destroy();

        /**
         * @brief Check if factory is valid
         */
        bool isValid() const { return !destroyed_.load(); }

    private:
        template <typename MessageT>
        std::shared_ptr<Topic<MessageT>> getOrCreateTopic(const std::string& topic_name, const QoS& qos)
        {
            std::string key = topic_name + "_" + typeid(MessageT).name();
            auto it        = topics_.find(key);
            if (it != topics_.end())
            {
                return std::static_pointer_cast<Topic<MessageT>>(it->second);
            }

            // Create new topic
            auto topic = std::make_shared<Topic<MessageT>>(topic_name, qos);
            topics_[key] = std::static_pointer_cast<void>(topic);
            return topic;
        }

        std::string name_;
        std::atomic<bool> destroyed_{false};

        // Type-erased storage for topics, subscriptions, and publishers
        std::unordered_map<std::string, std::shared_ptr<void>> topics_;
        std::vector<std::shared_ptr<void>> subscriptions_;
        std::vector<std::shared_ptr<void>> publishers_;
    };

}    // namespace arch::experimental

#endif    // !ARCH_COMM_FACTORY_H

