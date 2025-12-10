#ifndef MESSAGES_H
#define MESSAGES_H

#include <memory>
#include <string>
#include <typeinfo>
#include <utility>

class IMessage
{
protected:
    IMessage() noexcept = default;

public:
    IMessage(const IMessage&) = default;
    IMessage& operator=(const IMessage&) = default;
    IMessage(IMessage&&) noexcept        = default;
    IMessage& operator=(IMessage&&) noexcept = default;

    virtual std::string type_name() const = 0;
    virtual ~IMessage()                   = default;
};

template <typename T>
struct Message : public IMessage
{
    T data;

    Message() = default;
    explicit Message(T&& d) : data(std::move(d)) {}
    explicit Message(const T& d) : data(d) {}
    template <typename... Args>
    explicit Message(Args&&... args) : data(std::forward<Args>(args)...) {}

    std::string type_name() const override
    {
        return typeid(T).name();
    }
};

using IMessagePtr = std::shared_ptr<const IMessage>;

template <typename T>
using MessagePtr = std::shared_ptr<const Message<T>>;

template <typename MessageType>
using message_ptr = std::shared_ptr<MessageType>;

template <typename T, typename... Args>
MessagePtr<T> makeMessage(Args&&... args)
{
    static_assert(!std::is_same_v<T, IMessage>, "Cannot create Message<IMessage> directly");
    return std::make_shared<Message<T>>(std::forward<Args>(args)...);
}

template <typename MessageType, typename... Args>
inline message_ptr<MessageType> makeSharedMsg(Args&&... args) noexcept(
    std::is_nothrow_constructible_v<MessageType, Args&&...>)
{
    static_assert(std::is_base_of_v<IMessage, MessageType>, "Must derive from IMessage");
    return std::make_shared<MessageType>(std::forward<Args>(args)...);
}

#endif    // MESSAGES_H
