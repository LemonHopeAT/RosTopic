/**
 * @file lock_free_MPMC_queue.h
 * @brief Lock-free MPMC (Multiple Producer Multiple Consumer) bounded queue implementation
 * @date 15.12.2025
 * @version 1.0.0
 * @ingroup arch_experimental
 */

#pragma once
#ifndef ARCH_COMM_LOCK_FREE_MPMC_QUEUE_H
#define ARCH_COMM_LOCK_FREE_MPMC_QUEUE_H

#include <arch/communication/imessage.h>
#include <concurrency/impl/lock_free_MPMC_queue_impl.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>

namespace arch::experimental
{

    /**
     * @brief Lock-free MPMC (Multiple Producer Multiple Consumer) bounded queue
     * @ingroup arch_experimental
     * @tparam T Type of elements stored in queue (must be move-constructible and move-assignable)
     *
     * Vyukov MPMC bounded queue implementation. Uses standard sequence-based algorithm with
     * acquire/release ordering. For shared_ptr types, uses std::atomic<shared_ptr> (C++20)
     * for safer memory operations in lock-free context.
     * Use this when you need multiple producers or multiple consumers.
     * 
     * Uses PIMPL (Pointer to Implementation) pattern to hide implementation details.
     */
    template <typename T>
    class LockFreeMPMCQueue
    {
    public:
        /**
         * @brief Constructs MPMC queue with given capacity
         * @param capacity Queue capacity (will be rounded up to power of 2)
         * @throw std::invalid_argument if capacity is 0
         */
        explicit LockFreeMPMCQueue(size_t capacity)
        : pimpl_(std::make_unique<LockFreeMPMCQueueImpl<T>>(capacity))
        {
        }

        /**
         * @brief Constructs MPMC queue with external buffer via span
         * @param buffer_span Span over pre-allocated buffer (must have size >= capacity)
         * @param capacity Queue capacity (will be rounded up to power of 2)
         * @throw std::invalid_argument if capacity is 0 or buffer is too small
         * @note Buffer must remain valid for the lifetime of the queue
         * @note This constructor is deleted as span-based construction requires internal CellType
         *       which is not exposed. Use capacity-based constructor instead.
         */
        template <typename CellType>
        LockFreeMPMCQueue(std::span<CellType> buffer_span, size_t capacity) = delete;

        LockFreeMPMCQueue(const LockFreeMPMCQueue&) = delete;
        LockFreeMPMCQueue& operator=(const LockFreeMPMCQueue&) = delete;

        // Move constructor
        LockFreeMPMCQueue(LockFreeMPMCQueue&& other) noexcept = default;

        // Move assignment operator
        LockFreeMPMCQueue& operator=(LockFreeMPMCQueue&& other) noexcept = default;

        /**
         * @brief Push item into queue (non-blocking)
         * @param item Item to push (copied)
         * @return true if pushed successfully, false if queue is full
         */
        bool push(const T& item)
        {
            return pimpl_->push(item);
        }

        /**
         * @brief Push item into queue (non-blocking)
         * @param item Item to push (moved)
         * @return true if pushed successfully, false if queue is full
         */
        bool push(T&& item)
        {
            return pimpl_->push(std::move(item));
        }

        /**
         * @brief Emplace item into queue (non-blocking)
         * @param args Arguments to construct item
         * @return true if emplaced successfully, false if queue is full
         */
        template <typename... Args>
        bool emplace(Args&&... args)
        {
            return pimpl_->emplace(std::forward<Args>(args)...);
        }

        /**
         * @brief Pop item from queue (non-blocking)
         * @return Optional containing item if available, empty optional if queue is empty
         */
        std::optional<T> pop()
        {
            return pimpl_->pop();
        }

        /**
         * @brief Check if queue is empty
         * @return true if queue is empty, false otherwise
         */
        bool empty() const
        {
            return pimpl_->empty();
        }

        /**
         * @brief Get approximate current size (lock-free snapshot)
         * @return Current number of items in queue
         */
        size_t size() const
        {
            return pimpl_->size();
        }

        /**
         * @brief Get queue capacity
         * @return Queue capacity
         */
        size_t capacity() const noexcept
        {
            return pimpl_->capacity();
        }

    private:
        std::unique_ptr<LockFreeMPMCQueueImpl<T>> pimpl_;    ///< Pointer to implementation (PIMPL)
    };

}    // namespace arch::experimental

#endif    // !ARCH_COMM_LOCK_FREE_MPMC_QUEUE_H
