/**
 * @file lock_free_MPMC_queue_impl.h
 * @brief Lock-free MPMC (Multiple Producer Multiple Consumer) bounded queue implementation
 * @date 15.12.2025
 * @version 1.0.0
 * @ingroup arch_experimental
 */

#pragma once
#ifndef ARCH_COMM_LOCK_FREE_MPMC_QUEUE_IMPL_H
#define ARCH_COMM_LOCK_FREE_MPMC_QUEUE_IMPL_H

#include <arch/communication/imessage.h>
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
     * @brief Implementation class for lock-free MPMC queue (PIMPL pattern)
     * @ingroup arch_experimental
     * @tparam T Type of elements stored in queue (must be move-constructible and move-assignable)
     *
     * Internal implementation class containing all queue logic.
     * This is the actual implementation that will be hidden behind PIMPL pointer.
     */
    template <typename T>
    class LockFreeMPMCQueueImpl
    {
        static_assert(std::is_move_constructible<T>::value && std::is_move_assignable<T>::value,
                      "T must be move-constructible and move-assignable");

        // Check if T is a shared_ptr type
        template <typename U>
        struct is_shared_ptr : std::false_type
        {
        };

        template <typename U>
        struct is_shared_ptr<std::shared_ptr<U>> : std::true_type
        {
        };

        template <typename U>
        struct is_shared_ptr<const std::shared_ptr<U>> : std::true_type
        {
        };

        // Align Cell to cache line boundary to avoid false sharing
        struct alignas(64) Cell
        {
            std::atomic<size_t> seq;
            T data;
        };

        // Specialized Cell for shared_ptr types using atomic<shared_ptr> (C++20)
        template <typename U>
        struct CellAtomic
        {
            std::atomic<size_t> seq;
            std::atomic<U> data;    // C++20: atomic<shared_ptr> is supported

            CellAtomic() : seq(0), data(nullptr) {}
        };

        // Use specialized cell for shared_ptr types (C++20 only)
        using CellType = typename std::conditional<is_shared_ptr<T>::value,
                                                   CellAtomic<T>,
                                                   Cell>::type;

    public:
        /**
         * @brief Constructs MPMC queue with given capacity
         * @param capacity Queue capacity (will be rounded up to power of 2)
         * @throw std::invalid_argument if capacity is 0
         */
        explicit LockFreeMPMCQueueImpl(size_t capacity)
        : capacity_(round_up_pow2(capacity)), mask_(capacity_ - 1), buffer_storage_(std::make_unique<CellType[]>(capacity_)), buffer_(buffer_storage_.get(), capacity_)
        {
            if (capacity == 0)
                throw std::invalid_argument("capacity must be > 0");
            for (size_t i = 0; i < capacity_; ++i)
            {
                buffer_[i].seq.store(i, std::memory_order_relaxed);
                if constexpr (is_shared_ptr<T>::value)
                {
                    // Initialize atomic<shared_ptr> to nullptr
                    buffer_[i].data.store(T{}, std::memory_order_relaxed);
                }
            }
            enqueue_pos_.store(0, std::memory_order_relaxed);
            dequeue_pos_.store(0, std::memory_order_relaxed);
        }

        /**
         * @brief Constructs MPMC queue with external buffer via span
         * @param buffer_span Span over pre-allocated buffer (must have size >= capacity)
         * @param capacity Queue capacity (will be rounded up to power of 2)
         * @throw std::invalid_argument if capacity is 0 or buffer is too small
         * @note Buffer must remain valid for the lifetime of the queue
         */
        LockFreeMPMCQueueImpl(std::span<CellType> buffer_span, size_t capacity)
        : capacity_(round_up_pow2(capacity)), mask_(capacity_ - 1), buffer_(buffer_span)
        {
            if (capacity == 0)
                throw std::invalid_argument("capacity must be > 0");
            if (buffer_span.size() < capacity_)
                throw std::invalid_argument("buffer size must be >= capacity");

            for (size_t i = 0; i < capacity_; ++i)
            {
                buffer_[i].seq.store(i, std::memory_order_relaxed);
                if constexpr (is_shared_ptr<T>::value)
                {
                    // Initialize atomic<shared_ptr> to nullptr
                    buffer_[i].data.store(T{}, std::memory_order_relaxed);
                }
            }
            enqueue_pos_.store(0, std::memory_order_relaxed);
            dequeue_pos_.store(0, std::memory_order_relaxed);
        }

        LockFreeMPMCQueueImpl(const LockFreeMPMCQueueImpl&) = delete;
        LockFreeMPMCQueueImpl& operator=(const LockFreeMPMCQueueImpl&) = delete;

        // Move constructor - transfers ownership of buffer_storage_ and updates span
        LockFreeMPMCQueueImpl(LockFreeMPMCQueueImpl&& other) noexcept
        : capacity_(other.capacity_), mask_(other.mask_),
          buffer_storage_(std::move(other.buffer_storage_)),
          buffer_(buffer_storage_ ? std::span<CellType>(buffer_storage_.get(), capacity_) : other.buffer_),
          enqueue_pos_(other.enqueue_pos_.load(std::memory_order_relaxed)),
          dequeue_pos_(other.dequeue_pos_.load(std::memory_order_relaxed))
        {
            // Invalidate other's buffer span if it owned the storage
            if (buffer_storage_)
            {
                other.buffer_ = std::span<CellType>();
                // Reset other's positions to prevent use-after-move
                other.enqueue_pos_.store(0, std::memory_order_relaxed);
                other.dequeue_pos_.store(0, std::memory_order_relaxed);
            }
        }

        // Move assignment operator
        LockFreeMPMCQueueImpl& operator=(LockFreeMPMCQueueImpl&& other) noexcept
        {
            if (this != &other)
            {
                capacity_       = other.capacity_;
                mask_           = other.mask_;
                buffer_storage_ = std::move(other.buffer_storage_);
                buffer_         = buffer_storage_ ? std::span<CellType>(buffer_storage_.get(), capacity_) : other.buffer_;
                enqueue_pos_.store(other.enqueue_pos_.load(std::memory_order_relaxed), std::memory_order_relaxed);
                dequeue_pos_.store(other.dequeue_pos_.load(std::memory_order_relaxed), std::memory_order_relaxed);

                // Invalidate other's buffer span if it owned the storage
                if (buffer_storage_)
                {
                    other.buffer_ = std::span<CellType>();
                    // Reset other's positions to prevent use-after-move
                    other.enqueue_pos_.store(0, std::memory_order_relaxed);
                    other.dequeue_pos_.store(0, std::memory_order_relaxed);
                }
            }
            return *this;
        }

        /**
         * @brief Push item into queue (non-blocking)
         * @param item Item to push (copied)
         * @return true if pushed successfully, false if queue is full
         */
        bool push(const T& item) { return emplace(item); }

        /**
         * @brief Push item into queue (non-blocking)
         * @param item Item to push (moved)
         * @return true if pushed successfully, false if queue is full
         */
        bool push(T&& item) { return emplace(std::move(item)); }

        /**
         * @brief Emplace item into queue (non-blocking)
         * @param args Arguments to construct item
         * @return true if emplaced successfully, false if queue is full
         */
        template <typename... Args>
        bool emplace(Args&&... args)
        {
            size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
            for (;;)
            {
                CellType& cell = buffer_[pos & mask_];
                size_t seq     = cell.seq.load(std::memory_order_acquire);
                intptr_t dif   = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
                if (dif == 0)
                {
                    // Use acquire-release for CAS to ensure proper synchronization
                    if (enqueue_pos_.compare_exchange_weak(pos, pos + 1,
                                                           std::memory_order_acq_rel,
                                                           std::memory_order_acquire))
                    {
                        // For shared_ptr types, use atomic store (C++20)
                        if constexpr (is_shared_ptr<T>::value)
                        {
                            // C++20: Atomic store for shared_ptr - safer memory visibility
                            cell.data.store(T(std::forward<Args>(args)...), std::memory_order_release);
                        }
                        else
                        {
                            // For non-shared_ptr types, use regular assignment
                            // seq.store(release) below ensures data visibility
                            cell.data = T(std::forward<Args>(args)...);
                        }
                        // publish: make data visible before seq update
                        // Use release to ensure all writes to data are visible
                        // This release store synchronizes with acquire load in pop()
                        cell.seq.store(pos + 1, std::memory_order_release);
                        return true;
                    }
                    // else retry with updated pos
                }
                else if (dif < 0)
                {
                    // queue full
                    return false;
                }
                else
                {
                    pos = enqueue_pos_.load(std::memory_order_relaxed);    // retry with fresh tail
                }
            }
        }

        /**
         * @brief Pop item from queue (non-blocking)
         * @return Optional containing item if available, empty optional if queue is empty
         */
        std::optional<T> pop()
        {
            size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
            for (;;)
            {
                CellType& cell = buffer_[pos & mask_];
                size_t seq     = cell.seq.load(std::memory_order_acquire);
                intptr_t dif   = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
                if (dif == 0)
                {
                    // Use acquire-release for CAS to ensure proper synchronization
                    if (dequeue_pos_.compare_exchange_weak(pos, pos + 1,
                                                           std::memory_order_acq_rel,
                                                           std::memory_order_acquire))
                    {
                        // For shared_ptr types, use atomic load (C++20)
                        T res;
                        if constexpr (is_shared_ptr<T>::value)
                        {
                            // C++20: Atomic load for shared_ptr - safer memory visibility
                            res = cell.data.load(std::memory_order_acquire);
                            // Clear the atomic shared_ptr
                            cell.data.store(T{}, std::memory_order_relaxed);
                        }
                        else
                        {
                            // For non-shared_ptr types, use regular move
                            res = std::move(cell.data);
                        }
                        // mark slot as free for next round
                        // Use release to ensure all operations are visible
                        // This release store synchronizes with acquire load in emplace()
                        cell.seq.store(pos + capacity_, std::memory_order_release);
                        return std::optional<T>(std::move(res));
                    }
                    // else retry
                }
                else if (dif < 0)
                {
                    // empty
                    return std::nullopt;
                }
                else
                {
                    pos = dequeue_pos_.load(std::memory_order_relaxed);
                }
            }
        }

        bool empty() const
        {
            size_t head = dequeue_pos_.load(std::memory_order_acquire);
            size_t tail = enqueue_pos_.load(std::memory_order_acquire);
            return tail == head;
        }

        // approximate current size (lock-free snapshot)
        size_t size() const
        {
            size_t head = dequeue_pos_.load(std::memory_order_acquire);
            size_t tail = enqueue_pos_.load(std::memory_order_acquire);
            if (tail >= head)
                return tail - head;
            // wrap-around guard (shouldn't normally happen for monotonic counters)
            return (std::numeric_limits<size_t>::max() - head + tail + 1);
        }

        size_t capacity() const noexcept { return capacity_; }

    private:
        static size_t round_up_pow2(size_t x)
        {
            if (x == 0)
                return 1;
            --x;
            for (size_t i = 1; i < sizeof(size_t) * 8; i <<= 1)
                x |= x >> i;
            return ++x;
        }

        const size_t capacity_;
        const size_t mask_;
        std::unique_ptr<CellType[]> buffer_storage_;    ///< Owned buffer storage (nullptr if using external buffer)
        std::span<CellType> buffer_;                    ///< Span view over buffer (owned or external)
        alignas(64) std::atomic_size_t enqueue_pos_{0};
        alignas(64) std::atomic_size_t dequeue_pos_{0};
    };
}    // namespace arch::experimental

#endif    // !ARCH_COMM_LOCK_FREE_MPMC_QUEUE_IMPL_H
