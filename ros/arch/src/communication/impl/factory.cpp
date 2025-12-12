/**
 * @file factory.cpp
 * @brief Factory implementation
 * @date 15.12.2025
 * @version 1.0.0
 */

#include "communication/impl/factory.h"
#include <typeinfo>

namespace arch::experimental
{

    Factory::Factory(const std::string& name) : name_(name), destroyed_(false) {}

    Factory::~Factory() { destroy(); }

    void Factory::destroy()
    {
        bool expected = false;
        destroyed_.compare_exchange_strong(expected, true);
        // Factory no longer stores entities, so nothing to clear
    }

}    // namespace arch::experimental
