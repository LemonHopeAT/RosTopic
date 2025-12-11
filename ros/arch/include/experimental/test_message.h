/**
 * @file test_message.h
 * @brief Test message wrapper for debugging and benchmarking
 * @date 2025
 * @version 1.0.0
 * @ingroup arch_experimental
 * 
 * @note This file is for debugging/benchmarking purposes only.
 *       Use Message<T> from i_message.h for production code.
 */

#ifndef ARCH_EXPERIMENTAL_TEST_MESSAGE_H
#define ARCH_EXPERIMENTAL_TEST_MESSAGE_H

#include "arch/communication/imessage.h"
#include <memory>
#include <string>
#include <typeinfo>
#include <utility>

namespace arch::experimental
{

    /**
     * @brief Test message wrapper for type T (debugging/benchmarking only)
     * @ingroup arch_experimental
     * @tparam T Type of message data
     *
     * Wraps data type T in a message that inherits from arch::IMessage.
     * This class is intended for debugging and benchmarking purposes only.
     */
    template <typename T>
    struct TestMessage : public IMessage
    {
        T data;    ///< Message data

        TestMessage() = default;
        explicit TestMessage(T&& d) : data(std::move(d)) {}
        explicit TestMessage(const T& d) : data(d) {}

        /**
         * @brief Construct message from arguments
         * @param args Arguments to construct data
         */
        template <typename... Args>
        explicit TestMessage(Args&&... args) : data(std::forward<Args>(args)...) {}

        /**
         * @brief Get message type name
         * @return Type name string
         */
        std::string type_name() const
        {
            return std::string("TestMessage<") + typeid(T).name() + ">";
        }
    };

    /**
     * @brief Create test message pointer from arguments (for debugging/benchmarking only)
     * @tparam T Message data type
     * @tparam Args Argument types
     * @param args Arguments to construct message
     * @return Shared pointer to test message as IMessagePtr
     */
    template <typename T, typename... Args>
    message_ptr<T> makeTestMessage(Args&&... args)
    {
        return std::make_shared<const TestMessage<T>>(std::forward<Args>(args)...);
    }

}    // namespace arch::experimental

#endif    // !ARCH_EXPERIMENTAL_TEST_MESSAGE_H

