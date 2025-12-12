/**
 * @file callback_group.h
 * @brief Callback group for thread synchronization of callbacks
 * @date 15.12.2025
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
        CallbackGroup(Type type, const std::string& name = "");

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
        void enter();

        /**
         * @brief Leave callback execution (release lock for MutuallyExclusive)
         */
        void leave();

    private:
        Type type_;                              ///< Group type
        std::string name_;                       ///< Group name
        std::atomic_flag flag_ = ATOMIC_FLAG_INIT;    ///< Lock-free flag for MutuallyExclusive
    };

}    // namespace arch::experimental

#endif    // !ARCH_CCR_A_CALLBACK_GROUP_H
