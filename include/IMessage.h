/**
 * @file IMessage.h
 * @brief Message interface and message type definitions
 * @date 2024
 * @version 1.0.0
 * @ingroup arch_experimental
 */

#ifndef MESSAGES_H
#define MESSAGES_H

#include <memory>
#include <string>
#include <typeinfo>
#include <utility>

namespace arch::experimental
{

    /**
     * @brief Base interface for all messages
     * @ingroup arch_experimental
     */
    class IMessage
    {
    protected:
        IMessage() noexcept = default;

    public:
        IMessage(const IMessage&) = default;
        IMessage& operator=(const IMessage&) = default;
        IMessage(IMessage&&) noexcept        = default;
        IMessage& operator=(IMessage&&) noexcept = default;

        /**
         * @brief Get message type name
         * @return Type name string
         */
        virtual std::string type_name() const = 0;
        virtual ~IMessage()                   = default;
    };

    /**
     * @brief Message wrapper for type T
     * @ingroup arch_experimental
     * @tparam T Type of message data
     */
    template <typename T>
    struct Message : public IMessage
    {
        T data;    ///< Message data

        Message() = default;
        explicit Message(T&& d) : data(std::move(d)) {}
        explicit Message(const T& d) : data(d) {}
        
        /**
         * @brief Construct message from arguments
         * @param args Arguments to construct data
         */
        template <typename... Args>
        explicit Message(Args&&... args) : data(std::forward<Args>(args)...) {}

        std::string type_name() const override
        {
            return typeid(T).name();
        }
    };

    using IMessagePtr = std::shared_ptr<const IMessage>;    ///< Pointer to base message interface

    /**
     * @brief Pointer to typed message
     * @tparam T Message data type
     */
    template <typename T>
    using MessagePtr = std::shared_ptr<const Message<T>>;

    /**
     * @brief Pointer to message type
     * @tparam MessageType Message type
     */
    template <typename MessageType>
    using message_ptr = std::shared_ptr<MessageType>;

    /**
     * @brief Create message pointer from arguments
     * @tparam T Message data type
     * @tparam Args Argument types
     * @param args Arguments to construct message
     * @return Shared pointer to message
     */
    template <typename T, typename... Args>
    MessagePtr<T> makeMessage(Args&&... args)
    {
        static_assert(!std::is_same_v<T, IMessage>, "Cannot create Message<IMessage> directly");
        return std::make_shared<Message<T>>(std::forward<Args>(args)...);
    }

    /**
     * @brief Create shared message pointer from arguments
     * @tparam MessageType Message type (must derive from IMessage)
     * @tparam Args Argument types
     * @param args Arguments to construct message
     * @return Shared pointer to message
     */
    template <typename MessageType, typename... Args>
    inline message_ptr<MessageType> makeSharedMsg(Args&&... args) noexcept(
        std::is_nothrow_constructible_v<MessageType, Args&&...>)
    {
        static_assert(std::is_base_of_v<IMessage, MessageType>, "Must derive from IMessage");
        return std::make_shared<MessageType>(std::forward<Args>(args)...);
    }

}    // namespace arch::experimental

#endif    // MESSAGES_H
