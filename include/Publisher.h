/**
 * @file Publisher.h
 * @brief Publisher for publishing messages to topics
 * @date 2024
 * @version 1.0.0
 * @ingroup arch_experimental
 */

#ifndef ARCH_COMM_A_PUBLISHER_H
#define ARCH_COMM_A_PUBLISHER_H

#include "IMessage.h"
#include "Topic.h"
#include <memory>

namespace arch::experimental
{

    /**
     * @brief Publisher for publishing messages to topics
     * @ingroup arch_experimental
     * @tparam MessageT Type of message data
     */
    template <typename MessageT>
    class Publisher
    {
    public:
        /**
         * @brief Constructs publisher for given topic
         * @param topic Shared pointer to topic
         */
        Publisher(std::shared_ptr<Topic<MessageT>> topic) : topic_(std::move(topic)) {}

        /**
         * @brief Publish message pointer
         * @param msg Message pointer to publish
         * @return true if published successfully, false otherwise
         */
        bool notify(MessagePtr<MessageT> msg) { return topic_ ? topic_->publish(msg) : false; }
        
        /**
         * @brief Publish message data (copied)
         * @param data Message data to publish
         * @return true if published successfully, false otherwise
         */
        bool notify(const MessageT& data) { return notify(makeMessage<MessageT>(data)); }
        
        /**
         * @brief Publish message data (moved)
         * @param data Message data to publish
         * @return true if published successfully, false otherwise
         */
        bool notify(MessageT&& data) { return notify(makeMessage<MessageT>(std::move(data))); }

        /**
         * @brief Publish message constructed from arguments
         * @param args Arguments to construct message
         * @return true if published successfully, false otherwise
         */
        template <typename... Args>
        bool publish(Args&&... args) { return notify(makeMessage<MessageT>(std::forward<Args>(args)...)); }

        /**
         * @brief Get topic name
         * @return Topic name string (empty if topic is null)
         */
        const std::string& topic_name() const { return topic_ ? topic_->name() : empty_; }

    private:
        std::shared_ptr<Topic<MessageT>> topic_;    ///< Topic to publish to
        static inline std::string empty_;            ///< Empty string for null topic
    };

}    // namespace arch::experimental

#endif    // ARCH_COMM_A_PUBLISHER_H
