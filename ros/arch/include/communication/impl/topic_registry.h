/**
 * @file topic_registry.h
 * @brief Global registry for topics (singleton)
 * @date 15.12.2025
 * @version 1.0.0
 * @ingroup arch_experimental
 */

#ifndef ARCH_COMM_TOPIC_REGISTRY_H
#define ARCH_COMM_TOPIC_REGISTRY_H

#include "topic.h"
#include <memory>
#include <mutex>
#include <string>
#include <typeinfo>
#include <unordered_map>

namespace arch::experimental
{

    /**
     * @brief Global registry for topics (singleton)
     * @ingroup arch_experimental
     * 
     * Stores topics globally so multiple Factory instances can share the same topics.
     * Topics are managed by shared_ptr and automatically destroyed when last reference is released.
     */
    class TopicRegistry
    {
    public:
        /**
         * @brief Get singleton instance
         * @return Reference to TopicRegistry instance
         */
        static TopicRegistry& instance()
        {
            static TopicRegistry inst;
            return inst;
        }

        /**
         * @brief Get or create topic
         * @tparam Type Type of message data
         * @param topic_name Topic name
         * @param qos Quality of Service settings
         * @return Shared pointer to topic
         */
        template <typename Type>
        std::shared_ptr<Topic<Type>> getOrCreateTopic(const std::string& topic_name, const QoS& qos)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            
            std::string key = topic_name + "_" + typeid(Type).name();
            auto it = topics_.find(key);
            if (it != topics_.end())
            {
                return std::static_pointer_cast<Topic<Type>>(it->second);
            }

            // Create new topic
            auto topic = std::make_shared<Topic<Type>>(topic_name, qos);
            topics_[key] = std::static_pointer_cast<void>(topic);
            return topic;
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
            std::lock_guard<std::mutex> lock(mutex_);
            
            std::string key = topic_name + "_" + typeid(Type).name();
            auto it = topics_.find(key);
            if (it != topics_.end())
            {
                return std::static_pointer_cast<Topic<Type>>(it->second);
            }
            return nullptr;
        }

        /**
         * @brief Remove topic from registry (called when topic is destroyed)
         * @tparam Type Type of message data
         * @param topic_name Topic name
         */
        template <typename Type>
        void removeTopic(const std::string& topic_name)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            
            std::string key = topic_name + "_" + typeid(Type).name();
            topics_.erase(key);
        }

    private:
        TopicRegistry() = default;
        ~TopicRegistry() = default;
        TopicRegistry(const TopicRegistry&) = delete;
        TopicRegistry& operator=(const TopicRegistry&) = delete;

        mutable std::mutex mutex_;
        std::unordered_map<std::string, std::shared_ptr<void>> topics_;
    };

}    // namespace arch::experimental

#endif    // !ARCH_COMM_TOPIC_REGISTRY_H

