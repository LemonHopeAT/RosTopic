#ifndef ARCH_COMM_SUBSCRIBER_SLOT_H
#define ARCH_COMM_SUBSCRIBER_SLOT_H

#include "CallbackGroup.h"
#include "IMessage.h"
#include "Qos.h"
//#include "arch/concurrency/lock_free_mpsc_queue.h"
//#include "arch/concurrency/lock_free_spsc_queue.h"
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <arch/utils.h>

#include <deque>

#include <atomic>
#include <cassert>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <vector>

using std::chrono_literals::operator""ms;
namespace arch
{
    template <typename T>
    class LockFreeMPMCQueue
    {
        static_assert(std::is_move_constructible<T>::value && std::is_move_assignable<T>::value,
                      "T must be move-constructible and move-assignable");

    private:
        // cache-line sized cell to avoid false sharing between seq and data
        struct alignas(64) Cell
        {
            std::atomic_size_t seq;
            T data;
        };

        const size_t capacity_;
        const size_t mask_;
        std::vector<Cell> buffer_;

        // Put these on separate cache lines to avoid false-sharing between producers/consumers
        alignas(64) std::atomic_size_t enqueue_pos_{0};
        char pad0[64 - sizeof(std::atomic_size_t) > 0 ? 64 - sizeof(std::atomic_size_t) : 1];

        alignas(64) std::atomic_size_t dequeue_pos_{0};
        char pad1[64 - sizeof(std::atomic_size_t) > 0 ? 64 - sizeof(std::atomic_size_t) : 1];

        // helper: round up to next power of two
        static size_t round_up_pow2(size_t x)
        {
            if (x == 0)
                return 1;
            --x;
            for (size_t i = 1; i < sizeof(size_t) * 8; i <<= 1)
                x |= x >> i;
            return ++x;
        }

    public:
        explicit LockFreeMPMCQueue(size_t capacity)
        : capacity_(round_up_pow2(capacity)),
          mask_(capacity_ - 1),
          buffer_(capacity_)
        {
            if (capacity == 0)
                throw std::invalid_argument("capacity must be > 0");

            // init sequence numbers
            for (size_t i = 0; i < capacity_; ++i)
            {
                buffer_[i].seq.store(i, std::memory_order_relaxed);
            }
        }

        LockFreeMPMCQueue(const LockFreeMPMCQueue&) = delete;
        LockFreeMPMCQueue& operator=(const LockFreeMPMCQueue&) = delete;

        ~LockFreeMPMCQueue() = default;

        // non-blocking try_push: returns true if pushed, false if queue full
        bool push(const T& item)
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
                        cell.data = item;
                        cell.seq.store(pos + 1, std::memory_order_release);
                        return true;
                    }
                    else
                    {
                        continue;
                    }
                }
                else if (dif < 0)
                {
                    // queue full
                    return false;
                }
                else
                {
                    pos = enqueue_pos_.load(std::memory_order_relaxed);
                }
            }
        }

        bool push(T&& item)
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
                        cell.data = std::move(item);
                        cell.seq.store(pos + 1, std::memory_order_release);
                        return true;
                    }
                    else
                    {
                        continue;
                    }
                }
                else if (dif < 0)
                {
                    std::this_thread::sleep_for(1ms);
                    // return false;
                }
                else
                {
                    pos = enqueue_pos_.load(std::memory_order_relaxed);
                }
            }
        }

        // non-blocking try_pop: returns true if popped into 'out', false if empty
        bool pop(T& out)
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
                        out = std::move(cell.data);
                        // mark slot as free for next round (pos + capacity_)
                        cell.seq.store(pos + capacity_, std::memory_order_release);
                        return true;
                    }
                    else
                    {
                        continue;
                    }
                }
                else if (dif < 0)
                {
                    // empty
                    return false;
                }
                else
                {
                    pos = dequeue_pos_.load(std::memory_order_relaxed);
                }
            }
        }

        std::optional<T> pop()
        {
            T tmp;
            if (pop(tmp))
                return std::move(tmp);
            return std::nullopt;
        }

        // approximate size (may be racy)
        size_t size() const
        {
            size_t enq = enqueue_pos_.load(std::memory_order_acquire);
            size_t deq = dequeue_pos_.load(std::memory_order_acquire);
            return (enq >= deq) ? (enq - deq) : 0;
        }
        size_t capacity() const noexcept { return capacity_; }
    };

    template <typename T>
    class MutexQueue
    {
    public:
        MutexQueue(size_t /*capacity*/ = 1024) {}

        // push возвращает true если успешно, false — при переполнении (мы не ограничиваем)
        bool push(T v)
        {
            std::lock_guard<std::mutex> lk(m_);
            q_.push_back(std::move(v));
            return true;
        }

        // pop возвращает optional<T>
        std::optional<T> pop()
        {
            std::lock_guard<std::mutex> lk(m_);
            if (q_.empty())
                return std::nullopt;
            T v = std::move(q_.front());
            q_.pop_front();
            return v;
        }

        size_t size() const
        {
            std::lock_guard<std::mutex> lk(m_);
            return q_.size();
        }

    private:
        mutable std::mutex m_;
        std::deque<T> q_;
    };

    template <typename MessageT>
    class SubscriberSlot final
    {
    public:
        using Callback     = std::function<void(MessagePtr<MessageT>)>;
        using MessageQueue = MutexQueue<MessagePtr<MessageT>>;

    private:
        // Простой wrapper вокруг std::function с атомарным флагом
        class SafeCallback
        {
        private:
            Callback callback_;
            std::atomic<bool> valid_{true};
            std::atomic<int> ref_count_{0};    // Счетчик активных вызовов

        public:
            SafeCallback() = default;
            explicit SafeCallback(Callback cb) : callback_(std::move(cb)) {}

            SafeCallback(const SafeCallback&) = delete;
            SafeCallback& operator=(const SafeCallback&) = delete;
            SafeCallback(SafeCallback&&)                 = delete;
            SafeCallback& operator=(SafeCallback&&) = delete;

            ~SafeCallback() = default;

            void execute(MessagePtr<MessageT> msg)
            {
                if (!valid_.load(std::memory_order_acquire) || !callback_)
                    return;

                ref_count_.fetch_add(1, std::memory_order_acq_rel);

                try
                {
                    callback_(std::move(msg));
                }
                catch (...)
                {
                    // Игнорируем исключения
                }

                ref_count_.fetch_sub(1, std::memory_order_acq_rel);
            }

            void invalidate()
            {
                valid_.store(false, std::memory_order_release);

                // Ждем завершения всех активных вызовов
                int spins = 0;
                while (ref_count_.load(std::memory_order_acquire) > 0 && spins < 1000)
                {
                    spins++;
#ifdef __x86_64__
                    __builtin_ia32_pause();
#endif
                    arch::sleep_for(std::chrono::nanoseconds(100));
                }
            }

            bool valid() const
            {
                return valid_.load(std::memory_order_acquire);
            }
        };

    private:
        SafeCallback callback_;
        std::shared_ptr<CallbackGroup> group_;
        QoS qos_;
        std::string consumer_group_;
        MessageQueue queue_;
        std::atomic<bool> destroyed_{false};

    public:
        SubscriberSlot(Callback callback,
                       std::shared_ptr<CallbackGroup> group,
                       const QoS& qos,
                       const std::string& consumer_group = "")
        : callback_(std::move(callback)), group_(std::move(group)), qos_(qos), consumer_group_(consumer_group), queue_(qos.history_depth)
        {
        }

        // Запрещаем копирование
        SubscriberSlot(const SubscriberSlot&) = delete;
        SubscriberSlot& operator=(const SubscriberSlot&) = delete;

        // Move запрещаем для простоты
        SubscriberSlot(SubscriberSlot&&) = delete;
        SubscriberSlot& operator=(SubscriberSlot&&) = delete;

        ~SubscriberSlot()
        {
            destroy();
        }

        bool push_message(MessagePtr<MessageT> msg)
        {
            if (destroyed_.load(std::memory_order_acquire) || !callback_.valid())
                return false;

            // Если QoS надежный — доверяем очереди ответить что она полна.
            // Убираем дополнительный вызов size() (дорого/погрешно для lock-free очереди).
            if (qos_.reliability == QoS::Reliability::Reliable)
            {
                // попытка запушить — очередь сама вернёт false если полна
                return queue_.push(std::move(msg));
            }
            else
            {
                // для BestEffort можем отбросить при переполнении
                return queue_.push(std::move(msg));
            }
        }

        std::optional<MessagePtr<MessageT>> pop_message()
        {
            if (destroyed_.load(std::memory_order_acquire) || !callback_.valid())
                return std::nullopt;

            return queue_.pop();
        }

        void execute_callback(MessagePtr<MessageT> msg)    // Принимаем по значению!
        {
            if (destroyed_.load(std::memory_order_acquire) || !callback_.valid())
                return;

            if (!msg)
                return;

            try
            {
                if (group_)
                {
                    group_->enter();
                    try
                    {
                        callback_.execute(std::move(msg));    // Передаем владение
                    }
                    catch (...)
                    {
                        group_->leave();
                        throw;
                    }
                    group_->leave();
                }
                else
                {
                    callback_.execute(std::move(msg));    // Передаем владение
                }
            }
            catch (...)
            {
                // Игнорируем исключения в пользовательском коде
            }
        }

        bool has_messages() const
        {
            return !destroyed_.load(std::memory_order_acquire) &&
                   callback_.valid() &&
                   queue_.size() > 0;
        }

        size_t queue_size() const
        {
            if (destroyed_.load(std::memory_order_acquire) || !callback_.valid())
                return 0;
            return queue_.size();
        }

        const QoS& qos() const { return qos_; }
        const std::string& consumer_group() const { return consumer_group_; }
        std::shared_ptr<CallbackGroup> group() const { return group_; }

        bool valid() const
        {
            return !destroyed_.load(std::memory_order_acquire) && callback_.valid();
        }

        void destroy()
        {
            // Устанавливаем флаг destroyed
            destroyed_.store(true, std::memory_order_release);

            // Инвалидируем callback (он сам ждет завершения вызовов)
            callback_.invalidate();

            // Очищаем очередь
            clear_queue();
        }

    private:
        void clear_queue()
        {
            while (auto msg = queue_.pop())
            {
                // Освобождаем сообщения
            }
        }
    };

}    // namespace arch

#endif    // ARCH_COMM_SUBSCRIBER_SLOT_H
