/**
 * @file subscriber_slot.h
 * @brief Lock-free subscriber slot implementation with message queue and callback execution
 * @date 2025
 * @version 1.0.0
 * @ingroup arch_experimental
 */

#pragma once
#ifndef ARCH_COMM_SUBSCRIBER_SLOT_H
#define ARCH_COMM_SUBSCRIBER_SLOT_H

#include "callback_group.h"
#include "qos.h"
#include <arch/communication/imessage.h>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace arch::experimental
{

    /**
     * @brief Lock-free MPMC (Multiple Producer Multiple Consumer) bounded queue
     * @ingroup arch_experimental
     * @tparam T Type of elements stored in queue (must be move-constructible and move-assignable)
     *
     * Vyukov MPMC bounded queue implementation. Uses standard sequence-based algorithm with
     * acquire/release ordering. Works well with T = MessagePtr<TMsg> (std::shared_ptr),
     * because shared_ptr copy/assign is thread-safe.
     * Use this when you need multiple producers or multiple consumers.
     */
    template <typename T>
    class LockFreeMPMCQueue
    {
        static_assert(std::is_move_constructible<T>::value && std::is_move_assignable<T>::value,
                      "T must be move-constructible and move-assignable");

        // Align Cell to cache line boundary to avoid false sharing
        struct alignas(64) Cell
        {
            std::atomic<size_t> seq;
            T data;
        };

    public:
        /**
         * @brief Constructs MPMC queue with given capacity
         * @param capacity Queue capacity (will be rounded up to power of 2)
         * @throw std::invalid_argument if capacity is 0
         */
        explicit LockFreeMPMCQueue(size_t capacity)
        : capacity_(round_up_pow2(capacity)), mask_(capacity_ - 1), buffer_(capacity_)
        {
            if (capacity == 0)
                throw std::invalid_argument("capacity must be > 0");
            for (size_t i = 0; i < capacity_; ++i)
                buffer_[i].seq.store(i, std::memory_order_relaxed);
            enqueue_pos_.store(0, std::memory_order_relaxed);
            dequeue_pos_.store(0, std::memory_order_relaxed);
        }

        LockFreeMPMCQueue(const LockFreeMPMCQueue&) = delete;
        LockFreeMPMCQueue& operator=(const LockFreeMPMCQueue&) = delete;

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
                Cell& cell   = buffer_[pos & mask_];
                size_t seq   = cell.seq.load(std::memory_order_acquire);
                intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
                if (dif == 0)
                {
                    // Use acquire-release for CAS to ensure proper synchronization
                    if (enqueue_pos_.compare_exchange_weak(pos, pos + 1,
                                                           std::memory_order_acq_rel,
                                                           std::memory_order_acquire))
                    {
                        // For shared_ptr, assignment is thread-safe and efficient
                        cell.data = T(std::forward<Args>(args)...);
                        // publish: make data visible before seq update
                        // Use release to ensure all writes to data are visible
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
                Cell& cell   = buffer_[pos & mask_];
                size_t seq   = cell.seq.load(std::memory_order_acquire);
                intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
                if (dif == 0)
                {
                    // Use acquire-release for CAS to ensure proper synchronization
                    if (dequeue_pos_.compare_exchange_weak(pos, pos + 1,
                                                           std::memory_order_acq_rel,
                                                           std::memory_order_acquire))
                    {
                        // Move data out
                        T res = std::move(cell.data);
                        // mark slot as free for next round
                        // Use release to ensure all operations are visible
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
        std::vector<Cell> buffer_;
        alignas(64) std::atomic_size_t enqueue_pos_{0};
        alignas(64) std::atomic_size_t dequeue_pos_{0};
    };

    /**
     * @brief Subscriber slot with lock-free message queue and safe callback execution
     * @ingroup arch_experimental
     * @tparam MessageT Type of message data
     *
     * Manages a lock-free queue of MessagePtr<T> and provides safe invalidation
     * of callback without heavy locks. Supports QoS policies (Reliable/BestEffort)
     * and callback groups for thread-safe execution.
     */
    template <typename MessageT>
    class SubscriberSlot final
    {
    public:
        using Callback = std::function<void(message_ptr<const IMessage>)>;

    private:
        /**
         * @brief Lightweight synchronization for callback lifetime
         * @ingroup arch_experimental
         *
         * Uses valid_ flag + ref_count. execute() increments ref_count (acq_rel),
         * invalidate() sets valid_=false and waits until ref_count==0.
         */
        class SafeCallback
        {
        public:
            SafeCallback() = default;
            explicit SafeCallback(Callback cb) : callback_(std::move(cb)), valid_(true), ref_count_(0) {}

            SafeCallback(const SafeCallback&) = delete;
            SafeCallback& operator=(const SafeCallback&) = delete;

            void execute(message_ptr<const IMessage> msg)
            {
                // Fast-path check
                if (!valid_.load(std::memory_order_acquire))
                    return;

                // Increment active-call counter
                ref_count_.fetch_add(1, std::memory_order_acq_rel);

                // Re-check validity to handle the case where invalidate() happened between the first check and inc.
                if (!valid_.load(std::memory_order_acquire))
                {
                    // decrement and bail out
                    ref_count_.fetch_sub(1, std::memory_order_acq_rel);
                    return;
                }

                // Call user callback (catch exceptions)
                try
                {
                    if (callback_)
                        callback_(std::move(msg));
                }
                catch (...)
                {
                    // ignore
                }

                // Decrement active-call counter
                ref_count_.fetch_sub(1, std::memory_order_acq_rel);
            }

            void invalidate()
            {
                // Prevent further callers from entering
                valid_.store(false, std::memory_order_release);

                // Wait until all active calls finish.
                // Use backoff/yield to avoid busy spinning forever.
                int spins = 0;
                while (ref_count_.load(std::memory_order_acquire) != 0)
                {
                    ++spins;
                    if (spins < 10)
                    {
                        // busy short pause
#ifdef __x86_64__
                        __builtin_ia32_pause();
#endif
                    }
                    else if (spins < 100)
                    {
                        std::this_thread::yield();
                    }
                    else
                    {
                        std::this_thread::sleep_for(std::chrono::microseconds(50));
                    }
                }
                // Now safe to destroy/replace the std::function (caller will do it if needed).
                // Note: we keep callback_ alive to allow destructor semantics if required.
                // If you want to free callback_ memory here: callback_ = nullptr;
            }

            bool valid() const { return valid_.load(std::memory_order_acquire); }

        private:
            Callback callback_;
            std::atomic<bool> valid_{false};
            std::atomic<int> ref_count_{0};
        };

    public:
        /**
         * @brief Constructs subscriber slot
         * @param cb Callback function to execute when message is received
         * @param group Callback group for thread synchronization (can be nullptr)
         * @param qos Quality of Service settings
         * @param consumer_group Consumer group identifier (for SingleConsumer delivery)
         */
        SubscriberSlot(Callback cb,
                       std::shared_ptr<CallbackGroup> group,
                       const QoS& qos,
                       const std::string& consumer_group = "")
        : safe_cb_(std::move(cb)), group_(std::move(group)), qos_(qos), consumer_group_(consumer_group),
          queue_(std::max<size_t>(64, qos.history_depth))
        {
            // mark callback valid now that safe_cb_ constructed
            // (safe_cb_ constructor sets valid_ to false by default, set true here)
            // But we used constructor that sets true; if not, ensure:
            // safe_cb_.set_callback(...); but in our impl it's true after construction
        }

        SubscriberSlot(const SubscriberSlot&) = delete;
        SubscriberSlot& operator=(const SubscriberSlot&) = delete;

        ~SubscriberSlot()
        {
            destroy();
        }

        /**
         * @brief Push message into slot queue (called by Topic publish)
         * @param msg Message to push
         * @return true if pushed successfully, false otherwise
         * @note Fast path, lock-free. For Reliable QoS, retries a few times if queue is full.
         */
        bool push_message(message_ptr<const arch::IMessage> msg)
        {
            if (destroyed_.load(std::memory_order_acquire))
                return false;

            // For reliable QoS, retry a few times if queue is full
            if (qos_.reliability == QoS::Reliability::Reliable)
            {
                int retries           = 0;
                const int max_retries = 10;
                while (retries < max_retries)
                {
                    // Create a copy for this attempt (shared_ptr is cheap to copy)
                    message_ptr<const IMessage> msg_copy = msg;
                    if (queue_.push(std::move(msg_copy)))
                        return true;

                    // Queue full, yield and retry
                    std::this_thread::yield();
                    retries++;
                }
                // After retries, try one final time with original msg
                return queue_.push(std::move(msg));
            }
            else
            {
                // Best effort: just try once
                return queue_.push(std::move(msg));
            }
        }

        /**
         * @brief Pop one message from queue (used by Executor)
         * @return Optional containing message if available, empty optional otherwise
         */
        std::optional<message_ptr<const arch::IMessage>> pop_message()
        {
            if (destroyed_.load(std::memory_order_acquire))
                return std::nullopt;
            return queue_.pop();
        }

        /**
         * @brief Execute callback with proper callback_group enter/leave
         * @param msg Message to pass to callback
         */
        void execute_callback(message_ptr<const IMessage> msg)
        {
            if (destroyed_.load(std::memory_order_acquire))
                return;
            if (!safe_cb_.valid())
                return;
            if (!msg)
                return;

            if (group_)
            {
                group_->enter();
                safe_cb_.execute(std::move(msg));
                group_->leave();
            }
            else
            {
                safe_cb_.execute(std::move(msg));
            }
        }

        /**
         * @brief Check if queue has messages
         * @return true if queue has messages and slot is valid, false otherwise
         */
        bool has_messages() const
        {
            return !destroyed_.load(std::memory_order_acquire) && safe_cb_.valid() && !queue_.empty();
        }

        /**
         * @brief Get current queue size
         * @return Number of messages in queue (0 if destroyed or invalid)
         */
        size_t queue_size() const
        {
            if (destroyed_.load(std::memory_order_acquire) || !safe_cb_.valid())
                return 0;
            return queue_.size();
        }

        /**
         * @brief Get queue capacity
         * @return Maximum number of messages queue can hold
         */
        size_t queue_capacity() const { return queue_.capacity(); }

        /**
         * @brief Get QoS settings
         * @return Reference to QoS settings
         */
        const QoS& qos() const { return qos_; }

        /**
         * @brief Get consumer group identifier
         * @return Consumer group string
         */
        const std::string& consumer_group() const { return consumer_group_; }

        /**
         * @brief Get callback group
         * @return Shared pointer to callback group (can be nullptr)
         */
        std::shared_ptr<CallbackGroup> group() const { return group_; }

        /**
         * @brief Check if slot is valid
         * @return true if slot is not destroyed and callback is valid, false otherwise
         */
        bool valid() const { return !destroyed_.load(std::memory_order_acquire) && safe_cb_.valid(); }

        /**
         * @brief Destroy slot and invalidate callback
         * @note Waits for all active callbacks to finish before destroying
         */
        void destroy()
        {
            bool expected = false;
            if (!destroyed_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                return;

            // Prevent further callback entries and wait for active callers to finish
            safe_cb_.invalidate();

            // drain queue to release messages (shared_ptrs)
            clear_queue();
        }

    private:
        void clear_queue()
        {
            while (true)
            {
                auto maybe = queue_.pop();
                if (!maybe.has_value())
                    break;
                // let shared_ptr go out of scope
            }
        }

    private:
        SafeCallback safe_cb_;
        std::shared_ptr<CallbackGroup> group_;
        QoS qos_;
        std::string consumer_group_;
        // Use MPMC queue: supports multiple producers (multiple threads publishing to same topic)
        // and single consumer (executor thread). SPSC would be faster but doesn't support
        // the common case where multiple Publisher instances from different threads write to the same slot.
        LockFreeMPMCQueue<message_ptr<const IMessage>> queue_;
        std::atomic<bool> destroyed_{false};
    };

}    // namespace arch::experimental

#endif    // !ARCH_COMM_SUBSCRIBER_SLOT_H
