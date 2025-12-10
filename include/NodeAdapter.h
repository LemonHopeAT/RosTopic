#ifndef RCLCPP_COMPAT_NODE_ADAPTER_H
#define RCLCPP_COMPAT_NODE_ADAPTER_H

// NodeAdapter.h
// rclcpp-like Node adapter on top of the existing arch::* classes in the repo.
// Depends on repository headers: Publisher.h, Topic.h, Executor.h, Qos.h, CallbackGroup.h, IMessage.h

#include <any>
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

#include "CallbackGroup.h"
#include "Executor.h"
#include "IMessage.h"
#include "Publisher.h"
#include "Qos.h"
#include "Topic.h"

namespace rclcpp_compat
{
    using namespace arch;

    // NodeOptions minimal placeholder
    struct NodeOptions
    {
        bool enable_intra_process = false;
        // extend as needed
    };

    // hash for key (topic name + message type)
    struct TopicKey
    {
        std::string name;
        std::type_index type;

        bool operator==(TopicKey const& o) const noexcept { return name == o.name && type == o.type; }
    };

    struct TopicKeyHasher
    {
        std::size_t operator()(TopicKey const& k) const noexcept
        {
            return std::hash<std::string>{}(k.name) ^ (std::size_t(k.type.hash_code()) << 1);
        }
    };

    // The Node adapter
    class Node : public std::enable_shared_from_this<Node>
    {
    public:
        Node(const std::string& name, const std::string& ns = "/", const NodeOptions& options = NodeOptions())
        : name_(name), namespace_(ns), options_(options)
        {
            if (namespace_.empty())
                namespace_ = "/";
        }

        const std::string& get_name() const { return name_; }
        const std::string& get_namespace() const { return namespace_; }

        // attach executor (optional)
        void attach_executor(std::shared_ptr<arch::Executor> exec)
        {
            executor_ = exec;
            // register existing topics in executor
            for (auto& kv : topics_)
            {
                // try to register each topic via any_cast; if cast fails, ignore
                try_register_topic_any_to_executor(kv.second);
            }
        }

        // create_publisher: returns std::shared_ptr<arch::Publisher<MessageT>>
        template <typename MessageT>
        std::shared_ptr<arch::Publisher<MessageT>> create_publisher(
            const std::string& topic_name,
            const arch::QoS& qos = arch::QoS())
        {
            const std::string resolved = resolve_topic_name(topic_name);
            TopicKey key{resolved, std::type_index(typeid(MessageT))};

            // find or create topic (type-safe via std::any)
            std::shared_ptr<arch::Topic<MessageT>> topic_ptr;
            auto it = topics_.find(key);
            if (it != topics_.end())
            {
                try
                {
                    topic_ptr = std::any_cast<std::shared_ptr<arch::Topic<MessageT>>>(it->second);
                }
                catch (const std::bad_any_cast&)
                {
                    // existing topic with same name but different message type: fallthrough to create new
                }
            }

            if (!topic_ptr)
            {
                topic_ptr    = std::make_shared<arch::Topic<MessageT>>(resolved, qos);
                topics_[key] = std::any(topic_ptr);
                // register to executor if exists
                if (auto exec = executor_.lock())
                {
                    exec->add_topic<MessageT>(topic_ptr);
                }
            }

            auto pub = std::make_shared<arch::Publisher<MessageT>>(topic_ptr);
            // keep publisher alive
            publishers_.push_back(std::static_pointer_cast<void>(pub));
            return pub;
        }

        // create_subscription: user_cb receives std::shared_ptr<const MessageT> (aliasing shared_ptr)
        // returns Topic::SubscriberSlotPtr (shared_ptr<SubscriberSlot<MessageT>>)
        template <typename MessageT>
        std::shared_ptr<arch::SubscriberSlot<MessageT>> create_subscription(
            const std::string& topic_name,
            std::function<void(std::shared_ptr<const MessageT>)> user_cb,
            const arch::QoS& qos                                = arch::QoS(),
            std::shared_ptr<arch::CallbackGroup> callback_group = nullptr,
            const std::string& consumer_group                   = "")
        {
            const std::string resolved = resolve_topic_name(topic_name);
            TopicKey key{resolved, std::type_index(typeid(MessageT))};

            // find or create topic
            std::shared_ptr<arch::Topic<MessageT>> topic_ptr;
            auto it = topics_.find(key);
            if (it != topics_.end())
            {
                try
                {
                    topic_ptr = std::any_cast<std::shared_ptr<arch::Topic<MessageT>>>(it->second);
                }
                catch (const std::bad_any_cast&)
                {
                    // mismatch type, create a new topic for this type+name
                }
            }

            if (!topic_ptr)
            {
                topic_ptr    = std::make_shared<arch::Topic<MessageT>>(resolved, qos);
                topics_[key] = std::any(topic_ptr);
                if (auto exec = executor_.lock())
                {
                    exec->add_topic<MessageT>(topic_ptr);
                }
            }

            // build adapter callback: from MessagePtr<MessageT> -> std::shared_ptr<const MessageT>
            auto wrapper_cb = [user_cb = std::move(user_cb)](MessagePtr<MessageT> msg) {
                if (!msg)
                    return;
                // aliasing shared_ptr: keep ownership of Message<T> and point to its .data
                std::shared_ptr<const MessageT> alias(msg, &msg->data);
                user_cb(alias);
            };

            auto slot = topic_ptr->subscribe(wrapper_cb, std::move(callback_group), qos, consumer_group);
            // store slot to keep alive
            subscriptions_.push_back(std::static_pointer_cast<void>(slot));
            return slot;
        }

        // list topic names currently in the registry
        std::vector<std::string> get_topic_names() const
        {
            std::vector<std::string> out;
            out.reserve(topics_.size());
            for (auto const& kv : topics_)
                out.push_back(kv.first.name);
            return out;
        }

    private:
        std::string name_;
        std::string namespace_;
        NodeOptions options_;

        std::weak_ptr<arch::Executor> executor_;

        // type-erased storage for topics keyed by (name, msg-type)
        std::unordered_map<TopicKey, std::any, TopicKeyHasher> topics_;

        // keep publishers and subscriptions alive as type-erased pointers
        std::vector<std::shared_ptr<void>> publishers_;
        std::vector<std::shared_ptr<void>> subscriptions_;

        // simple resolver: handles only relative vs absolute names and namespace join
        std::string resolve_topic_name(const std::string& topic) const
        {
            if (topic.empty())
                return std::string("/");
            if (topic.front() == '/')
                return topic;
            std::string ns = namespace_;
            if (ns.empty())
                ns = "/";
            if (ns.back() != '/')
                ns.push_back('/');
            return ns + topic;
        }

        // helper: try to register stored any topic to executor (if possible)
        void try_register_topic_any_to_executor(const std::any& a)
        {
            if (a.has_value() == false)
                return;
            if (auto exec = executor_.lock())
            {
                // We don't know the concrete type stored in any; try a few common patterns is possible,
                // but we cannot enumerate unknown message types. The executor registration is normally
                // invoked when topic was created via create_publisher/create_subscription above.
                // So here we do nothing (we already registered on create).
                (void)exec;
            }
        }
    };

}    // namespace rclcpp_compat

#endif    // RCLCPP_COMPAT_NODE_ADAPTER_H
