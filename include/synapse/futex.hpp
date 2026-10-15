#pragma once

#include "synapse/common.hpp"
#include <atomic>
#include <chrono>

namespace synapse {

class Futex {
public:
    // Wakes at most one waiting thread/process
    static void wake_one(std::atomic<uint32_t>& word) noexcept;

    // Wakes all waiting threads/processes
    static void wake_all(std::atomic<uint32_t>& word) noexcept;

    // Waits until word != expected, using adaptive spin-wait followed by kernel sleep
    [[nodiscard]] static bool wait(
        std::atomic<uint32_t>& word,
        uint32_t expected,
        std::chrono::nanoseconds timeout = std::chrono::nanoseconds::max(),
        uint32_t spin_count = kDefaultSpinIterations
    ) noexcept;
};

} // namespace synapse
