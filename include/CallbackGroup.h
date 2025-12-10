#ifndef ARCH_CCR_A_CALLBACK_GROUP_H
#define ARCH_CCR_A_CALLBACK_GROUP_H

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace arch
{

    class CallbackGroup
    {
    public:
        enum class Type
        {
            MutuallyExclusive,
            Reentrant
        };

        CallbackGroup(Type type, const std::string& name = "")
        : type_(type), name_(name)
        {
            flag_.clear();
        }

        Type type() const { return type_; }
        const std::string& name() const { return name_; }

        void enter()
        {
            if (type_ == Type::MutuallyExclusive)
            {
                // Spin-lock без блокировок
                while (flag_.test_and_set(std::memory_order_acquire))
                {
                    // Можно добавить std::this_thread::yield() для снижения нагрузки
                    std::this_thread::yield();
                }
            }
            // Reentrant: ничего не делаем
        }

        void leave()
        {
            if (type_ == Type::MutuallyExclusive)
            {
                flag_.clear(std::memory_order_release);
            }
            // Reentrant: ничего не делаем
        }

    private:
        Type type_;
        std::string name_;
        std::atomic_flag flag_ = ATOMIC_FLAG_INIT;    // lock-free
    };

}    // namespace arch

#endif    // ARCH_CCR_A_CALLBACK_GROUP_H
