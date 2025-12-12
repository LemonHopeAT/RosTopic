/**
 * @file waitable.h
 * @brief Waitable interface for objects that can be processed by Executor
 * @date 15.12.2025
 * @version 1.0.0
 * @ingroup arch_experimental
 */

#ifndef ARCH_COMM_WAITABLE_H
#define ARCH_COMM_WAITABLE_H

#include "impl/wait_set.h"
#include <memory>

namespace arch::experimental
{

    /**
     * @brief Base interface for objects that can be processed by Executor
     * @ingroup arch_experimental
     *
     * In ROS2, Subscription, Timer, Service, Client, etc. all implement Waitable interface.
     * This allows Executor to uniformly process different types of events.
     */
    class Waitable
    {
    public:
        virtual ~Waitable() = default;

        /**
         * @brief Check if waitable is ready to be executed
         * @return true if ready, false otherwise
         */
        virtual bool isReady() const = 0;

        /**
         * @brief Execute the waitable (process callback, etc.)
         * @note Called by Executor when waitable is ready
         */
        virtual void execute() = 0;

        /**
         * @brief Add this waitable to wait set for notification
         * @param wait_set Wait set to add to
         */
        virtual void addToWaitSet(WaitSet& wait_set) = 0;

        /**
         * @brief Remove this waitable from wait set
         * @param wait_set Wait set to remove from
         */
        virtual void removeFromWaitSet(WaitSet& wait_set) = 0;

        /**
         * @brief Check if waitable is valid (not destroyed)
         * @return true if valid, false otherwise
         */
        virtual bool isValid() const = 0;
    };

    /**
     * @brief Base class for entities (Publisher, Subscription, Timer, etc.)
     * @ingroup arch_experimental
     *
     * Provides common lifecycle management for ROS2-like entities.
     */
    class Entity
    {
    public:
        virtual ~Entity() = default;

        /**
         * @brief Destroy the entity
         * @note After destroy(), isValid() should return false
         */
        virtual void destroy() = 0;

        /**
         * @brief Check if entity is valid (not destroyed)
         * @return true if valid, false otherwise
         */
        virtual bool isValid() const = 0;
    };

}    // namespace arch::experimental

#endif    // !ARCH_COMM_WAITABLE_H


