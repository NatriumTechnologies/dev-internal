#include "synapse/futex.hpp"

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <synchapi.h>
    #pragma comment(lib, "synchronization.lib")
#else
    #include <sys/syscall.h>
    #include <unistd.h>
    #include <linux/futex.h>
    #include <sys/time.h>
    #include <cerrno>
#endif

namespace synapse {

void Futex::wake_one(std::atomic<uint32_t>& word) noexcept {
#if defined(_WIN32)
    WakeByAddressSingle(const_cast<uint32_t*>(reinterpret_cast<const uint32_t*>(&word)));
#else
    syscall(
        SYS_futex,
        reinterpret_cast<int*>(const_cast<uint32_t*>(reinterpret_cast<const uint32_t*>(&word))),
        FUTEX_WAKE_PRIVATE,
        1,
        nullptr,
        nullptr,
        0
    );
#endif
}

void Futex::wake_all(std::atomic<uint32_t>& word) noexcept {
#if defined(_WIN32)
    WakeByAddressAll(const_cast<uint32_t*>(reinterpret_cast<const uint32_t*>(&word)));
#else
    syscall(
        SYS_futex,
        reinterpret_cast<int*>(const_cast<uint32_t*>(reinterpret_cast<const uint32_t*>(&word))),
        FUTEX_WAKE_PRIVATE,
        INT32_MAX,
        nullptr,
        nullptr,
        0
    );
#endif
}

bool Futex::wait(
    std::atomic<uint32_t>& word,
    uint32_t expected,
    std::chrono::nanoseconds timeout,
    uint32_t spin_count
) noexcept {
    // Stage 1: Adaptive user-space spin-pause loop
    for (uint32_t i = 0; i < spin_count; ++i) {
        if (word.load(std::memory_order_relaxed) != expected) {
            return true;
        }
        cpu_relax();
    }

    if (word.load(std::memory_order_acquire) != expected) {
        return true;
    }

    if (timeout <= std::chrono::nanoseconds::zero()) {
        return word.load(std::memory_order_acquire) != expected;
    }

    // Stage 2: Fall back to native kernel sleep primitive
#if defined(_WIN32)
    DWORD ms = INFINITE;
    if (timeout != std::chrono::nanoseconds::max()) {
        const auto ms_count = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();
        ms = (ms_count <= 0) ? 1 : static_cast<DWORD>(ms_count);
    }

    uint32_t comp = expected;
    const BOOL ret = WaitOnAddress(
        const_cast<uint32_t*>(reinterpret_cast<const uint32_t*>(&word)),
        &comp,
        sizeof(uint32_t),
        ms
    );

    if (!ret && GetLastError() == ERROR_TIMEOUT) {
        return word.load(std::memory_order_acquire) != expected;
    }
    return true;

#else
    struct timespec ts{};
    struct timespec* pts = nullptr;

    if (timeout != std::chrono::nanoseconds::max()) {
        const auto secs = std::chrono::duration_cast<std::chrono::seconds>(timeout);
        const auto nanos = timeout - secs;
        ts.tv_sec = static_cast<time_t>(secs.count());
        ts.tv_nsec = static_cast<long>(nanos.count());
        pts = &ts;
    }

    const long ret = syscall(
        SYS_futex,
        reinterpret_cast<int*>(const_cast<uint32_t*>(reinterpret_cast<const uint32_t*>(&word))),
        FUTEX_WAIT_PRIVATE,
        static_cast<int>(expected),
        pts,
        nullptr,
        0
    );

    if (ret == -1 && errno == ETIMEDOUT) {
        return word.load(std::memory_order_acquire) != expected;
    }
    return true;
#endif
}

} // namespace synapse
