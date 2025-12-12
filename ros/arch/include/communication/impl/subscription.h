/**
 * @file subscription.h
 * @brief Subscription class for subscribing to topics (ROS2-like API)
 * @date 15.12.2025
 * @version 1.0.0
 * @ingroup arch_experimental
 */

#ifndef ARCH_COMM_SUBSCRIPTION_H
#define ARCH_COMM_SUBSCRIPTION_H

#include "../waitable.h"
#include "callback_group.h"
#include "qos.h"
#include "subscriber_slot.h"
#include "topic.h"
#include <arch/communication/imessage.h>
#include <functional>
#include <memory>
#include <string>

namespace arch::experimental
{

    /**
     * @brief Subscription to a topic (ROS2-like API)
     * @ingroup arch_experimental
     * @tparam Type Type of message data
     *
     * Represents a subscription to a topic. Similar to rclcpp::Subscription in ROS2.
     * Manages the lifecycle of SubscriberSlot and provides ROS2-like interface.
     */
    template <typename Type>
    class Subscription : public Waitable
    {
    public:
        using Callback = std::function<void(message_ptr<const IMessage>)>;

        /**
         * @brief Constructs subscription
         * @param topic Shared pointer to topic
         * @param callback Callback function to execute when message is received
         * @param group Callback group for thread synchronization (can be nullptr)
         * @param qos Quality of Service settings
         * @param consumer_group Consumer group identifier (for SingleConsumer delivery)
         */
        Subscription(std::shared_ptr<Topic<Type>> topic,
                     Callback callback,
                     std::shared_ptr<CallbackGroup> group = nullptr,
                     QoS qos                              = QoS(),
                     const std::string& consumer_group    = "")
        : topic_(std::move(topic)), slot_(nullptr), destroyed_(false)
        {
            if (topic_)
            {
                slot_ = topic_->subscribe(std::move(callback), std::move(group), qos, consumer_group);
            }
        }

        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;

        Subscription(Subscription&& other) noexcept
        : topic_(std::move(other.topic_)), slot_(std::move(other.slot_)), destroyed_(other.destroyed_.load())
        {
            other.destroyed_.store(true);
        }

        Subscription& operator=(Subscription&& other) noexcept
        {
            if (this != &other)
            {
                destroy();
                topic_     = std::move(other.topic_);
                slot_      = std::move(other.slot_);
                destroyed_ = other.destroyed_.load();
                other.destroyed_.store(true);
            }
            return *this;
        }

        ~Subscription() override { destroy(); }

        /**
         * @brief Get topic name
         * @return Topic name string
         */
        std::string get_topic_name() const
        {
            return topic_ ? topic_->get_topic_name() : std::string();
        }

        /**
         * @brief Get QoS settings
         * @return Reference to QoS settings
         */
        const QoS& getQoS() const
        {
            static const QoS empty_qos;
            return slot_ ? slot_->qos() : empty_qos;
        }

        /**
         * @brief Get queue size
         * @return Number of messages in queue
         */
        size_t getQueueSize() const
        {
            return slot_ ? slot_->queue_size() : 0;
        }

        /**
         * @brief Get queue capacity
         * @return Maximum number of messages queue can hold
         */
        size_t getQueueCapacity() const
        {
            return slot_ ? slot_->queue_capacity() : 0;
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
         * @brief Get number of subscriptions to this topic
         * @return Number of active subscriptions
         */
        size_t get_subscription_count() const
        {
            return topic_ ? topic_->get_subscription_count() : 0;
        }

        /**
         * @brief Get underlying topic
         * @return Shared pointer to topic (can be nullptr)
         */
        std::shared_ptr<Topic<Type>> get_topic() const
        {
            return topic_;
        }

        /**
         * @brief Destroy subscription
         */
        void destroy()
        {
            bool expected = false;
            if (!destroyed_.compare_exchange_strong(expected, true))
                return;

            if (slot_ && topic_)
            {
                topic_->unsubscribe(slot_);
                slot_.reset();
            }
        }

        /**
         * @brief Check if subscription is valid
         */
        bool isValid() const override
        {
            return !destroyed_.load() && slot_ != nullptr && topic_ != nullptr;
        }

        // Waitable interface
        bool isReady() const override
        {
            return isValid() && slot_ && slot_->has_messages();
        }

        void execute() override
        {
            if (!isValid() || !slot_)
                return;

            // Executor will call slot->execute_callback() directly
            // This method is here for Waitable interface compliance
        }

        void addToWaitSet(WaitSet& wait_set) override
        {
            // WaitSet integration can be added here if needed
            (void)wait_set;
        }

        void removeFromWaitSet(WaitSet& wait_set) override
        {
            // WaitSet integration can be added here if needed
            (void)wait_set;
        }

        /**
         * @brief Get underlying subscriber slot (for Executor use)
         * @return Shared pointer to subscriber slot (can be nullptr)
         */
        std::shared_ptr<SubscriberSlot<Type>> getSlot() const { return slot_; }

        /**
         * @brief Get QoS settings for this subscription
         * @return Reference to QoS settings
         */
        const QoS& get_qos() const
        {
            return getQoS();
        }

    private:
        std::shared_ptr<Topic<Type>> topic_;
        std::shared_ptr<SubscriberSlot<Type>> slot_;
        std::atomic<bool> destroyed_{false};
    };

}    // namespace arch::experimental

#endif    // !ARCH_COMM_SUBSCRIPTION_H
