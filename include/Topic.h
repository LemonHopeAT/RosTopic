#ifndef ARCH_COMM_TOPIC_H
#define ARCH_COMM_TOPIC_H

#include "Qos.h"
#include "SubscriberSlot.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace arch
{

    // Simple thread registry + epoch-based reclamation helper.
    // Optimized for fast reader path: readers only do atomic load of slots_ptr_ and
    // set/reset their thread-local epoch number (no locks).
    class EpochReclaimer
    {
    public:
        // singleton access
        static EpochReclaimer& instance()
        {
            static EpochReclaimer inst;
            return inst;
        }

        // Register current thread and return index to use for epoch array.
        // Uses mutex only on first-time registration for this thread.
        size_t register_thread()
        {
            // thread-local index, initialized on first call in this thread
            static thread_local size_t t_index = SIZE_MAX;
            if (t_index != SIZE_MAX)
                return t_index;

            std::lock_guard<std::mutex> lk(reg_mutex_);
            // assign index and allocate an atomic epoch for this thread
            t_index = epochs_.size();
            epochs_.push_back(new std::atomic<uint64_t>(kInactiveEpoch));
            return t_index;
        }

        // Called by reader threads when entering critical section (short-lived).
        void enter(size_t idx)
        {
            if (idx >= epochs_.size())
                return;
            uint64_t g = global_epoch_.load(std::memory_order_acquire);
            epochs_[idx]->store(g, std::memory_order_release);
        }

        // Called by reader threads on exit.
        void leave(size_t idx)
        {
            if (idx >= epochs_.size())
                return;
            epochs_[idx]->store(kInactiveEpoch, std::memory_order_release);
        }

        // Retire a pointer together with current epoch.
        template <typename T>
        void retire(T* ptr)
        {
            if (!ptr)
                return;
            uint64_t retire_epoch = global_epoch_.fetch_add(1, std::memory_order_acq_rel) + 1;
            {
                std::lock_guard<std::mutex> lk(retired_mutex_);
                retired_.emplace_back(reinterpret_cast<void*>(ptr), retire_epoch,
                                      [](void* p) { delete reinterpret_cast<T*>(p); });
            }
            try_reclaim();
        }

        ~EpochReclaimer()
        {
            // free remaining retired items
            std::vector<RetiredItem> to_free;
            {
                std::lock_guard<std::mutex> lk(retired_mutex_);
                to_free.swap(retired_);
            }
            for (auto& it : to_free)
                if (it.deleter)
                    it.deleter(it.ptr);

            // delete epoch atomics
            std::lock_guard<std::mutex> lk(reg_mutex_);
            for (auto p : epochs_)
                delete p;
            epochs_.clear();
        }

    private:
        EpochReclaimer() = default;

        void try_reclaim()
        {
            uint64_t min_epoch = std::numeric_limits<uint64_t>::max();
            {
                // snapshot epochs (vector growth protected by reg_mutex_)
                std::lock_guard<std::mutex> lk(reg_mutex_);
                for (auto p : epochs_)
                {
                    uint64_t v = p->load(std::memory_order_acquire);
                    if (v < min_epoch)
                        min_epoch = v;
                }
            }

            // if no threads registered, min_epoch == max then we can reclaim everything
            if (min_epoch == std::numeric_limits<uint64_t>::max())
                min_epoch = global_epoch_.load(std::memory_order_acquire);

            // move reclaimable to local vector to free without holding retired_mutex_ too long
            std::vector<RetiredItem> to_free;
            {
                std::lock_guard<std::mutex> lk(retired_mutex_);
                auto it = retired_.begin();
                while (it != retired_.end())
                {
                    if (it->epoch < min_epoch)
                    {
                        to_free.push_back(std::move(*it));
                        it = retired_.erase(it);
                    }
                    else
                        ++it;
                }
            }

            for (auto& item : to_free)
            {
                if (item.deleter)
                    item.deleter(item.ptr);
            }
        }

        struct RetiredItem
        {
            void* ptr;
            uint64_t epoch;
            std::function<void(void*)> deleter;
            RetiredItem(void* p, uint64_t e, std::function<void(void*)> d) : ptr(p), epoch(e), deleter(std::move(d)) {}
        };

        std::atomic<uint64_t> global_epoch_{1};
        std::mutex reg_mutex_;
        // store pointers to atomics because std::atomic is not copyable/movable
        std::vector<std::atomic<uint64_t>*> epochs_;    // per-thread epochs, kInactiveEpoch when not in critical section

        std::mutex retired_mutex_;
        std::vector<RetiredItem> retired_;

        static constexpr uint64_t kInactiveEpoch = std::numeric_limits<uint64_t>::max();
    };

    template <typename MessageT>
    class Topic : public std::enable_shared_from_this<Topic<MessageT>>
    {
    public:
        using SubscriberSlotPtr     = std::shared_ptr<SubscriberSlot<MessageT>>;
        using WeakSubscriberSlotPtr = std::weak_ptr<SubscriberSlot<MessageT>>;

        Topic(const std::string& name, const QoS& default_qos)
        : name_(name), default_qos_(default_qos), destroyed_(false), slots_ptr_(nullptr), rr_index_(0)
        {
            // initial empty vector
            auto vec = new std::vector<SubscriberSlotPtr>();
            slots_ptr_.store(vec, std::memory_order_release);
        }

        ~Topic()
        {
            destroy();
            auto ptr = slots_ptr_.exchange(nullptr, std::memory_order_acq_rel);
            if (ptr)
                EpochReclaimer::instance().retire(ptr);
        }

        void set_wake_callback(std::function<void()> cb)
        {
            std::lock_guard<std::mutex> lk(wake_mutex_);
            wake_cb_ = std::move(cb);
        }

        std::string name() const { return name_; }

        SubscriberSlotPtr subscribe(
            std::function<void(MessagePtr<MessageT>)> callback,
            std::shared_ptr<CallbackGroup> group = nullptr,
            QoS qos                              = QoS(),
            const std::string& consumer_group    = "")
        {
            if (destroyed_.load(std::memory_order_acquire))
                return nullptr;

            if (qos.history_depth == 0)
                qos.history_depth = default_qos_.history_depth;
            if (qos.delivery == QoS::Delivery::Broadcast)
                qos.delivery = default_qos_.delivery;

            auto slot = std::make_shared<SubscriberSlot<MessageT>>(std::move(callback), std::move(group), qos, consumer_group);

            // copy-on-write insertion with atomic ptr swap
            std::vector<SubscriberSlotPtr>* old_ptr = slots_ptr_.load(std::memory_order_acquire);
            for (;;)
            {
                auto new_vec = new std::vector<SubscriberSlotPtr>(*old_ptr);
                new_vec->push_back(slot);
                if (slots_ptr_.compare_exchange_weak(old_ptr, new_vec, std::memory_order_release, std::memory_order_acquire))
                {
                    // retire old vector safely
                    EpochReclaimer::instance().retire(old_ptr);
                    break;
                }
                // CAS failed: old_ptr updated to current, retry
                delete new_vec;
            }

            return slot;
        }

        void unsubscribe(const SubscriberSlotPtr& slot)
        {
            if (!slot || destroyed_.load(std::memory_order_acquire))
                return;

            slot->destroy();

            std::vector<SubscriberSlotPtr>* old_ptr = slots_ptr_.load(std::memory_order_acquire);
            for (;;)
            {
                auto new_vec = new std::vector<SubscriberSlotPtr>();
                new_vec->reserve(old_ptr->size());
                for (auto& s : *old_ptr)
                    if (s && s != slot)
                        new_vec->push_back(s);

                if (slots_ptr_.compare_exchange_weak(old_ptr, new_vec, std::memory_order_release, std::memory_order_acquire))
                {
                    EpochReclaimer::instance().retire(old_ptr);
                    break;
                }
                delete new_vec;
            }
        }

        // publish: fast path, lock-free reading of slots pointer
        bool publish(MessagePtr<MessageT> msg)
        {
            if (!msg || destroyed_.load(std::memory_order_acquire))
                return false;

            // register thread with reclaimer lazily
            size_t tidx = EpochReclaimer::instance().register_thread();

            // enter epoch -- mark ourselves as active
            EpochReclaimer::instance().enter(tidx);

            // load current slots pointer once
            std::vector<SubscriberSlotPtr>* slots = slots_ptr_.load(std::memory_order_acquire);
            if (!slots || slots->empty())
            {
                EpochReclaimer::instance().leave(tidx);
                return false;
            }

            // Delivery policy
            if (default_qos_.delivery == QoS::Delivery::Broadcast)
            {
                // broadcast to all subscribers
                for (auto& s : *slots)
                {
                    if (s)
                        s->push_message(msg);
                }
            }
            else    // SingleConsumer: round-robin
            {
                size_t idx = rr_index_.fetch_add(1, std::memory_order_relaxed);
                idx        = idx % slots->size();
                auto& s    = (*slots)[idx];
                if (s)
                    s->push_message(msg);
            }

            // leave epoch
            EpochReclaimer::instance().leave(tidx);

            // call wake callback (if set) without holding epoch
            std::function<void()> cb;
            {
                std::lock_guard<std::mutex> lk(wake_mutex_);
                cb = wake_cb_;
            }
            if (cb)
            {
                try
                {
                    cb();
                }
                catch (...)
                {
                }
            }

            return true;
        }

        std::vector<SubscriberSlotPtr> get_slots() const
        {
            auto ptr = slots_ptr_.load(std::memory_order_acquire);
            if (!ptr)
                return {};
            return *ptr;
        }

        void destroy()
        {
            bool expected = false;
            if (!destroyed_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                return;

            // exchange pointer with empty vector and retire the old one
            auto new_vec = new std::vector<SubscriberSlotPtr>();
            auto old_ptr = slots_ptr_.exchange(new_vec, std::memory_order_acq_rel);
            if (old_ptr)
            {
                // call destroy on slots to mark them dead
                for (auto& s : *old_ptr)
                {
                    if (s)
                        s->destroy();
                }
                EpochReclaimer::instance().retire(old_ptr);
            }

            // clear wake callback
            {
                std::lock_guard<std::mutex> lk(wake_mutex_);
                wake_cb_ = nullptr;
            }
        }

        size_t subscriber_count() const
        {
            auto ptr = slots_ptr_.load(std::memory_order_acquire);
            return ptr ? ptr->size() : 0;
        }

    private:
        std::string name_;
        QoS default_qos_;
        std::atomic<bool> destroyed_{false};

        // atomic raw pointer to vector<shared_ptr<SubscriberSlot<T>>>.
        // Writers swap with new vector and retire old pointer; readers just load pointer and iterate.
        std::atomic<std::vector<SubscriberSlotPtr>*> slots_ptr_;

        // wake callback and mutex for setting/calling it (we call callback without holding epoch)
        std::function<void()> wake_cb_;
        mutable std::mutex wake_mutex_;

        // round-robin index for SingleConsumer delivery
        std::atomic<uint64_t> rr_index_;
    };

}    // namespace arch

#endif    // ARCH_COMM_TOPIC_H
