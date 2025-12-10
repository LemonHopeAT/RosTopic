
#ifndef ARCH_COMM_TOPIC_H
#define ARCH_COMM_TOPIC_H

#include "Qos.h"
#include "SubscriberSlot.h"
#include <algorithm>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>

namespace arch
{

    template <typename MessageT>
    class Topic : public std::enable_shared_from_this<Topic<MessageT>>
    {
    public:
        using SubscriberSlotPtr     = std::shared_ptr<SubscriberSlot<MessageT>>;
        using WeakSubscriberSlotPtr = std::weak_ptr<SubscriberSlot<MessageT>>;

        Topic(const std::string& name, const QoS& default_qos)
        : name_(name), default_qos_(default_qos), destroyed_(false)
        {
            slots_ptr_.store(new std::vector<SubscriberSlotPtr>(), std::memory_order_release);
            wake_callback_ptr_.store(new std::function<void()>(), std::memory_order_release);
        }

        ~Topic()
        {
            destroy();

            auto slots_ptr = slots_ptr_.exchange(nullptr, std::memory_order_acq_rel);
            if (slots_ptr)
            {
                delete slots_ptr;
            }

            auto wake_cb = wake_callback_ptr_.exchange(nullptr, std::memory_order_acq_rel);
            if (wake_cb)
            {
                delete wake_cb;
            }
        }

        void set_wake_callback(std::function<void()> cb)
        {
            auto new_cb = new std::function<void()>(std::move(cb));
            auto old_cb = wake_callback_ptr_.exchange(new_cb, std::memory_order_acq_rel);
            if (old_cb)
            {
                delete old_cb;
            }
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

            // lock-free вставка через атомарный вектор копированием
            std::vector<SubscriberSlotPtr>* old_slots;
            std::vector<SubscriberSlotPtr>* new_slots;
            do
            {
                old_slots = slots_ptr_.load(std::memory_order_acquire);
                new_slots = new std::vector<SubscriberSlotPtr>(*old_slots);
                new_slots->push_back(slot);
            } while (!slots_ptr_.compare_exchange_weak(old_slots, new_slots, std::memory_order_release, std::memory_order_acquire));
            delete old_slots;

            return slot;
        }

        void unsubscribe(const SubscriberSlotPtr& slot)
        {
            if (!slot || destroyed_.load(std::memory_order_acquire))
                return;

            slot->destroy();

            std::vector<SubscriberSlotPtr>* old_slots;
            std::vector<SubscriberSlotPtr>* new_slots;
            do
            {
                old_slots = slots_ptr_.load(std::memory_order_acquire);
                new_slots = new std::vector<SubscriberSlotPtr>();
                for (auto& s : *old_slots)
                    if (s && s != slot)
                        new_slots->push_back(s);
            } while (!slots_ptr_.compare_exchange_weak(old_slots, new_slots, std::memory_order_release, std::memory_order_acquire));
            delete old_slots;
        }

        bool publish(MessagePtr<MessageT> msg)
        {
            if (destroyed_.load(std::memory_order_acquire) || !msg)
                return false;

            auto snapshot = slots_ptr_.load(std::memory_order_acquire);
            auto wake_cb  = wake_callback_ptr_.load(std::memory_order_acquire);

            bool success = true;

            for (auto& slot : *snapshot)
            {
                if (!slot)
                    continue;
                bool pushed = slot->push_message(msg);
                if (pushed && wake_cb && *wake_cb)
                    (*wake_cb)();
                else if (!pushed && slot->qos().reliability == QoS::Reliability::Reliable)
                    success = false;
            }

            return success;
        }

        void destroy()
        {
            destroyed_.store(true, std::memory_order_release);

            auto snapshot = slots_ptr_.load(std::memory_order_acquire);
            for (auto& slot : *snapshot)
                if (slot)
                    slot->destroy();
        }

        std::vector<SubscriberSlotPtr> get_slots()
        {
            auto snapshot = slots_ptr_.load(std::memory_order_acquire);
            std::vector<SubscriberSlotPtr> result;
            for (auto& s : *snapshot)
                if (s)
                    result.push_back(s);
            return result;
        }

    private:
        std::string name_;
        QoS default_qos_;
        std::atomic<bool> destroyed_{false};

        // Lock-free вектор слотов
        std::atomic<std::vector<SubscriberSlotPtr>*> slots_ptr_;

        // Lock-free wake_callback
        std::atomic<std::function<void()>*> wake_callback_ptr_{nullptr};
    };

}    // namespace arch

#endif    // ARCH_COMM_TOPIC_H
