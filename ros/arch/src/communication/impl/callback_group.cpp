/**
 * @file callback_group.cpp
 * @brief Callback group implementation
 * @date 2025
 * @version 1.0.0
 */

#include "include/communication/impl/callback_group.h"
#include <thread>

namespace arch::experimental
{

    CallbackGroup::CallbackGroup(Type type, const std::string& name)
    : type_(type), name_(name)
    {
        flag_.clear();
    }

    void CallbackGroup::enter()
    {
        if (type_ == Type::MutuallyExclusive)
        {
            while (flag_.test_and_set(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
        }
    }

    void CallbackGroup::leave()
    {
        if (type_ == Type::MutuallyExclusive)
        {
            flag_.clear(std::memory_order_release);
        }
    }

}    // namespace arch::experimental
