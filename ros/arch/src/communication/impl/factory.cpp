/**
 * @file factory.cpp
 * @brief Factory implementation
 * @date 2025
 * @version 1.0.0
 */

#include "include/communication/impl/factory.h"
#include <typeinfo>

namespace arch::experimental
{

    Factory::Factory(const std::string& name) : name_(name), destroyed_(false) {}

    Factory::~Factory() { destroy(); }

    void Factory::destroy()
    {
        bool expected = false;
        if (!destroyed_.compare_exchange_strong(expected, true))
            return;

        subscriptions_.clear();
        publishers_.clear();
        topics_.clear();
    }

}    // namespace arch::experimental
