#pragma once
#ifndef ARCH_COMM_SUBSCRIBER_SLOT_H
#define ARCH_COMM_SUBSCRIBER_SLOT_H

#include "CallbackGroup.h"
#include "IMessage.h"
#include "Qos.h"
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

namespace arch
{

    // Vyukov MPMC bounded queue (T must be move-assignable / copy-assignable).
    // Uses standard sequence-based algorithm with acquire/release ordering.
    // Works well with T = MessagePtr<TMsg> (std::shared_ptr), because shared_ptr copy/assign is thread-safe.
    template <typename T>
    class LockFreeMPMCQueue
    {
        static_assert(std::is_move_constructible<T>::value && std::is_move_assignable<T>::value,
                      "T must be move-constructible and move-assignable");

        struct Cell
        {
            std::atomic<size_t> seq;
            T data;
        };

    public:
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

        ~LockFreeMPMCQueue() = default;

        // try to push (copy)
        bool push(const T& item) { return emplace(item); }

        // try to push (move)
        bool push(T&& item) { return emplace(std::move(item)); }

        // emplace: non-blocking; returns false if full
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
                    if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed, std::memory_order_relaxed))
                    {
                        // construct/assign in-place
                        cell.data = T(std::forward<Args>(args)...);
                        // publish: make data visible before seq update
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

        // try_pop: returns optional<T>
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
                    if (dequeue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed, std::memory_order_relaxed))
                    {
                        T res = std::move(cell.data);
                        // mark slot as free for next round
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

    // SubscriberSlot: lock-free queue of MessagePtr<T>, safe invalidation of callback without heavy locks.
    template <typename MessageT>
    class SubscriberSlot final
    {
    public:
        using MessagePtrT = MessagePtr<MessageT>;
        using Callback    = std::function<void(MessagePtrT)>;

    private:
        // SafeCallback: lightweight synchronization for callback lifetime.
        // Idea: valid_ flag + ref_count. execute() increments ref_count (acq_rel),
        // invalidate() sets valid_=false and waits until ref_count==0.
        class SafeCallback
        {
        public:
            SafeCallback() = default;
            explicit SafeCallback(Callback cb) : callback_(std::move(cb)), valid_(true), ref_count_(0) {}

            SafeCallback(const SafeCallback&) = delete;
            SafeCallback& operator=(const SafeCallback&) = delete;

            void execute(MessagePtrT msg)
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

        // push message into slot queue (called by Topic publish). Fast path, lock-free.
        bool push_message(MessagePtrT msg)
        {
            if (destroyed_.load(std::memory_order_acquire))
                return false;
            // For reliable QoS we attempt push and return false if full.
            // For best-effort, we also push and let caller decide on dropping.
            return queue_.push(std::move(msg));
        }

        // pop one message (used by Executor). Returns optional<MessagePtr<T>>
        std::optional<MessagePtrT> pop_message()
        {
            if (destroyed_.load(std::memory_order_acquire))
                return std::nullopt;
            return queue_.pop();
        }

        // Execute callback with proper callback_group enter/leave
        void execute_callback(MessagePtrT msg)
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

        bool has_messages() const
        {
            return !destroyed_.load(std::memory_order_acquire) && safe_cb_.valid() && !queue_.empty();
        }

        size_t queue_size() const
        {
            if (destroyed_.load(std::memory_order_acquire) || !safe_cb_.valid())
                return 0;
            return queue_.size();
        }

        size_t queue_capacity() const { return queue_.capacity(); }

        const QoS& qos() const { return qos_; }
        const std::string& consumer_group() const { return consumer_group_; }
        std::shared_ptr<CallbackGroup> group() const { return group_; }

        bool valid() const { return !destroyed_.load(std::memory_order_acquire) && safe_cb_.valid(); }

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
        LockFreeMPMCQueue<MessagePtrT> queue_;
        std::atomic<bool> destroyed_{false};
    };

}    // namespace arch

#endif    // ARCH_COMM_SUBSCRIBER_SLOT_H
