
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

    // EpochReclaimer: lock-free registration via singly-linked list of per-thread nodes.
    // Each thread allocates one Node and pushes it to head_ on first register.
    // Readers call enter(node)/leave(node) using their thread-local node pointer.
    class EpochReclaimer
    {
    public:
        struct Node
        {
            std::atomic<uint64_t> epoch;
            Node* next;
            Node(uint64_t v) : epoch(v), next(nullptr) {}
        };

        static EpochReclaimer& instance()
        {
            static EpochReclaimer inst;
            return inst;
        }

        // Return thread-local Node*; create and push to list on first call (lock-free push_front).
        Node* register_thread()
        {
            static thread_local Node* t_node = nullptr;
            if (t_node)
                return t_node;

            Node* n = new Node(kInactiveEpoch);

            // push front: lock-free CAS on head_
            Node* old_head = head_.load(std::memory_order_acquire);
            for (;;)
            {
                n->next = old_head;
                if (head_.compare_exchange_weak(old_head, n, std::memory_order_release, std::memory_order_acquire))
                    break;
                // old_head updated by CAS failure; retry
            }

            t_node = n;
            return t_node;
        }

        void enter(Node* n)
        {
            if (!n)
                return;
            uint64_t g = global_epoch_.load(std::memory_order_acquire);
            n->epoch.store(g, std::memory_order_release);
        }

        void leave(Node* n)
        {
            if (!n)
                return;
            n->epoch.store(kInactiveEpoch, std::memory_order_release);
        }

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
            // free retired
            std::vector<RetiredItem> to_free;
            {
                std::lock_guard<std::mutex> lk(retired_mutex_);
                to_free.swap(retired_);
            }
            for (auto& it : to_free)
                if (it.deleter)
                    it.deleter(it.ptr);

            // delete nodes in linked list
            Node* cur = head_.load(std::memory_order_acquire);
            while (cur)
            {
                Node* next = cur->next;
                delete cur;
                cur = next;
            }
            head_.store(nullptr, std::memory_order_relaxed);
        }

    private:
        EpochReclaimer() = default;

        void try_reclaim()
        {
            uint64_t min_epoch = std::numeric_limits<uint64_t>::max();

            // scan linked list for min epoch (safe: list nodes are stable)
            Node* cur = head_.load(std::memory_order_acquire);
            while (cur)
            {
                uint64_t v = cur->epoch.load(std::memory_order_acquire);
                if (v < min_epoch)
                    min_epoch = v;
                cur = cur->next;
            }

            if (min_epoch == std::numeric_limits<uint64_t>::max())
                min_epoch = global_epoch_.load(std::memory_order_acquire);

            // move reclaimable to local vector to free without holding retired_mutex_
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
                if (item.deleter)
                    item.deleter(item.ptr);
        }

        struct RetiredItem
        {
            void* ptr;
            uint64_t epoch;
            std::function<void(void*)> deleter;
            RetiredItem(void* p, uint64_t e, std::function<void(void*)> d) : ptr(p), epoch(e), deleter(std::move(d)) {}
        };

        std::atomic<uint64_t> global_epoch_{1};

        // head of singly-linked list of Node*, each node allocated once per thread and not moved until program end
        std::atomic<Node*> head_{nullptr};

        std::mutex retired_mutex_;
        std::vector<RetiredItem> retired_;
        static constexpr uint64_t kInactiveEpoch = std::numeric_limits<uint64_t>::max();
    };

    // Topic: stores raw pointers to SubscriberSlot in hot-path vector to avoid shared_ptr refcounting.
    template <typename MessageT>
    class Topic : public std::enable_shared_from_this<Topic<MessageT>>
    {
    public:
        using SubscriberSlotPtr = std::shared_ptr<SubscriberSlot<MessageT>>;
        using SubscriberSlotRaw = SubscriberSlot<MessageT>*;

        Topic(const std::string& name, const QoS& default_qos)
        : name_(name), default_qos_(default_qos), destroyed_(false), slots_ptr_(nullptr), wake_ptr_(nullptr), rr_index_(0)
        {
            auto vec = new std::vector<SubscriberSlotRaw>();
            slots_ptr_.store(vec, std::memory_order_release);
        }

        ~Topic()
        {
            destroy();
            auto p = slots_ptr_.exchange(nullptr, std::memory_order_acq_rel);
            if (p)
                EpochReclaimer::instance().retire(p);
            auto wp = wake_ptr_.exchange(nullptr, std::memory_order_acq_rel);
            if (wp)
                EpochReclaimer::instance().retire(reinterpret_cast<std::function<void()>*>(wp));
        }

        std::string name() const { return name_; }

        // set wake callback — atomically swap pointer to heap-allocated std::function
        void set_wake_callback(std::function<void()> cb)
        {
            auto new_cb = new std::function<void()>(std::move(cb));
            void* old   = wake_ptr_.exchange(reinterpret_cast<void*>(new_cb), std::memory_order_acq_rel);
            if (old)
                EpochReclaimer::instance().retire(reinterpret_cast<std::function<void()>*>(old));
        }

        // subscribe: create slot (shared_ptr owner returned to caller), push raw pointer to internal vector (COW)
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

            auto slot                               = std::make_shared<SubscriberSlot<MessageT>>(std::move(callback), std::move(group), qos, consumer_group);
            std::vector<SubscriberSlotRaw>* old_ptr = slots_ptr_.load(std::memory_order_acquire);
            for (;;)
            {
                auto new_vec = new std::vector<SubscriberSlotRaw>(*old_ptr);
                new_vec->push_back(slot.get());
                if (slots_ptr_.compare_exchange_weak(old_ptr, new_vec, std::memory_order_release, std::memory_order_acquire))
                {
                    EpochReclaimer::instance().retire(old_ptr);
                    break;
                }
                delete new_vec;
            }
            return slot;
        }

        void unsubscribe(const SubscriberSlotPtr& slot)
        {
            if (!slot || destroyed_.load(std::memory_order_acquire))
                return;
            slot->destroy();

            std::vector<SubscriberSlotRaw>* old_ptr = slots_ptr_.load(std::memory_order_acquire);
            for (;;)
            {
                auto new_vec = new std::vector<SubscriberSlotRaw>();
                new_vec->reserve(old_ptr->size());
                for (auto s : *old_ptr)
                    if (s && s != slot.get())
                        new_vec->push_back(s);

                if (slots_ptr_.compare_exchange_weak(old_ptr, new_vec, std::memory_order_release, std::memory_order_acquire))
                {
                    EpochReclaimer::instance().retire(old_ptr);
                    break;
                }
                delete new_vec;
            }
        }

        // publish: single atomic load of slots pointer + iteration over raw pointers; minimal overhead
        bool publish(MessagePtr<MessageT> msg)
        {
            if (!msg || destroyed_.load(std::memory_order_acquire))
                return false;

            auto node = EpochReclaimer::instance().register_thread();
            EpochReclaimer::instance().enter(node);

            auto slots = slots_ptr_.load(std::memory_order_acquire);
            if (!slots || slots->empty())
            {
                EpochReclaimer::instance().leave(node);
                return false;
            }

            if (default_qos_.delivery == QoS::Delivery::Broadcast)
            {
                for (auto s : *slots)
                {
                    if (s)
                        s->push_message(msg);
                }
            }
            else    // SingleConsumer: round-robin
            {
                size_t idx = rr_index_.fetch_add(1, std::memory_order_relaxed);
                idx        = idx % slots->size();
                auto s     = (*slots)[idx];
                if (s)
                    s->push_message(msg);
            }

            EpochReclaimer::instance().leave(node);

            // wake callback: atomic load of function pointer and direct call (no mutex)
            auto wp = reinterpret_cast<std::function<void()>*>(wake_ptr_.load(std::memory_order_acquire));
            if (wp && *wp)
            {
                try
                {
                    (*wp)();
                }
                catch (...)
                {
                }
            }

            return true;
        }

        std::vector<SubscriberSlotRaw> get_slots() const
        {
            auto p = slots_ptr_.load(std::memory_order_acquire);
            if (!p)
                return {};
            return *p;
        }

        void destroy()
        {
            bool expected = false;
            if (!destroyed_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                return;

            auto new_vec = new std::vector<SubscriberSlotRaw>();
            auto old_ptr = slots_ptr_.exchange(new_vec, std::memory_order_acq_rel);
            if (old_ptr)
            {
                for (auto s : *old_ptr)
                    if (s)
                        s->destroy();
                EpochReclaimer::instance().retire(old_ptr);
            }

            auto old_w = wake_ptr_.exchange(nullptr, std::memory_order_acq_rel);
            if (old_w)
                EpochReclaimer::instance().retire(reinterpret_cast<std::function<void()>*>(old_w));
        }

        size_t subscriber_count() const
        {
            auto p = slots_ptr_.load(std::memory_order_acquire);
            return p ? p->size() : 0;
        }

    private:
        std::string name_;
        QoS default_qos_;
        std::atomic<bool> destroyed_{false};

        std::atomic<std::vector<SubscriberSlotRaw>*> slots_ptr_;    // raw-pointer to vector; retired via EpochReclaimer
        std::atomic<void*> wake_ptr_;                               // atomic pointer to heap std::function<void()>, retired via EpochReclaimer

        std::atomic<uint64_t> rr_index_;
    };

}    // namespace arch

#endif    // ARCH_COMM_TOPIC_H
