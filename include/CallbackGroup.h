/**
 * @file CallbackGroup.h
 * @brief Callback group for thread synchronization of callbacks
 * @date 2024
 * @version 1.0.0
 * @ingroup arch_experimental
 */

#ifndef ARCH_CCR_A_CALLBACK_GROUP_H
#define ARCH_CCR_A_CALLBACK_GROUP_H

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace arch::experimental
{

    /**
     * @brief Callback group for thread synchronization of callbacks
     * @ingroup arch_experimental
     *
     * Provides synchronization for callback execution. Supports two modes:
     * - MutuallyExclusive: Only one callback can execute at a time
     * - Reentrant: Multiple callbacks can execute concurrently
     */
    class CallbackGroup
    {
    public:
        /**
         * @brief Callback group type
         */
        enum class Type
        {
            MutuallyExclusive,    ///< Only one callback executes at a time
            Reentrant            ///< Multiple callbacks can execute concurrently
        };

        /**
         * @brief Constructs callback group
         * @param type Group type (MutuallyExclusive or Reentrant)
         * @param name Group name (optional)
         */
        CallbackGroup(Type type, const std::string& name = "")
        : type_(type), name_(name)
        {
            flag_.clear();
        }

        /**
         * @brief Get group type
         * @return Group type
         */
        Type type() const { return type_; }
        
        /**
         * @brief Get group name
         * @return Group name string
         */
        const std::string& name() const { return name_; }

        /**
         * @brief Enter callback execution (acquire lock for MutuallyExclusive)
         */
        void enter()
        {
            if (type_ == Type::MutuallyExclusive)
            {
                // Spin-lock без блокировок
                while (flag_.test_and_set(std::memory_order_acquire))
                {
                    // Можно добавить std::this_thread::yield() для снижения нагрузки
                    std::this_thread::yield();
                }
            }
            // Reentrant: ничего не делаем
        }

        /**
         * @brief Leave callback execution (release lock for MutuallyExclusive)
         */
        void leave()
        {
            if (type_ == Type::MutuallyExclusive)
            {
                flag_.clear(std::memory_order_release);
            }
            // Reentrant: ничего не делаем
        }

    private:
        Type type_;                              ///< Group type
        std::string name_;                       ///< Group name
        std::atomic_flag flag_ = ATOMIC_FLAG_INIT;    ///< Lock-free flag for MutuallyExclusive
    };

}    // namespace arch::experimental

#endif    // ARCH_CCR_A_CALLBACK_GROUP_H
