#ifndef ARCH_COMM_A_PUBLISHER_H
#define ARCH_COMM_A_PUBLISHER_H

#include "IMessage.h"
#include "Topic.h"
#include <memory>

namespace arch
{

    template <typename MessageT>
    class Publisher
    {
    public:
        Publisher(std::shared_ptr<Topic<MessageT>> topic) : topic_(std::move(topic)) {}

        bool notify(MessagePtr<MessageT> msg) { return topic_ ? topic_->publish(msg) : false; }
        bool notify(const MessageT& data) { return notify(makeMessage<MessageT>(data)); }
        bool notify(MessageT&& data) { return notify(makeMessage<MessageT>(std::move(data))); }

        template <typename... Args>
        bool publish(Args&&... args) { return notify(makeMessage<MessageT>(std::forward<Args>(args)...)); }

        const std::string& topic_name() const { return topic_ ? topic_->name() : empty_; }

    private:
        std::shared_ptr<Topic<MessageT>> topic_;
        static inline std::string empty_;
    };

}    // namespace arch

#endif    // ARCH_COMM_A_PUBLISHER_H
