#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <chrono>
#include <atomic>
#include <optional>
#include <utility>

#if defined(_MSC_VER)
    #include <intrin.h>
    #define SYNAPSE_FORCE_INLINE __forceinline
    #define SYNAPSE_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    #include <x86intrin.h>
    #define SYNAPSE_FORCE_INLINE inline __attribute__((always_inline))
    #define SYNAPSE_NOINLINE __attribute__((noinline))
#else
    #define SYNAPSE_FORCE_INLINE inline
    #define SYNAPSE_NOINLINE
#endif

namespace synapse {

// Standard L1 data cacheline boundary across x86-64 and modern ARM64
inline constexpr size_t kCacheLineSize = 64;

// Default spin loop limit before yielding / futex wait
inline constexpr uint32_t kDefaultSpinIterations = 1024;

enum class Status : int32_t {
    Ok = 0,
    BufferFull = -1,
    BufferEmpty = -2,
    Timeout = -3,
    ShmCreationFailed = -4,
    ShmMapFailed = -5,
    InvalidMagic = -6,
    VersionMismatch = -7,
    PayloadTooLarge = -8,
    ChecksumMismatch = -9,
    ChannelClosed = -10,
    InvalidArgument = -11
};

[[nodiscard]] SYNAPSE_FORCE_INLINE constexpr const char* status_to_string(Status status) noexcept {
    switch (status) {
        case Status::Ok: return "Ok";
        case Status::BufferFull: return "Ring buffer is full";
        case Status::BufferEmpty: return "Ring buffer is empty";
        case Status::Timeout: return "Operation timed out";
        case Status::ShmCreationFailed: return "Failed to create shared memory region";
        case Status::ShmMapFailed: return "Failed to map shared memory view";
        case Status::InvalidMagic: return "Invalid channel header magic signature";
        case Status::VersionMismatch: return "Protocol version incompatible";
        case Status::PayloadTooLarge: return "Payload exceeds maximum slot capacity";
        case Status::ChecksumMismatch: return "Payload CRC32 checksum mismatch";
        case Status::ChannelClosed: return "Channel has been closed or unmapped";
        case Status::InvalidArgument: return "Invalid argument provided";
        default: return "Unknown status";
    }
}

template <typename T>
class Result {
public:
    constexpr Result(T value) noexcept : value_(std::move(value)), status_(Status::Ok) {}
    constexpr Result(Status status) noexcept : value_(std::nullopt), status_(status) {}

    [[nodiscard]] constexpr bool ok() const noexcept { return status_ == Status::Ok; }
    [[nodiscard]] constexpr Status status() const noexcept { return status_; }

    [[nodiscard]] constexpr const T& value() const & noexcept { return *value_; }
    [[nodiscard]] constexpr T& value() & noexcept { return *value_; }
    [[nodiscard]] constexpr T&& value() && noexcept { return std::move(*value_); }

    [[nodiscard]] constexpr const T* operator->() const noexcept { return &(*value_); }
    [[nodiscard]] constexpr T* operator->() noexcept { return &(*value_); }

private:
    std::optional<T> value_;
    Status status_;
};

template <>
class Result<void> {
public:
    constexpr Result() noexcept : status_(Status::Ok) {}
    constexpr Result(Status status) noexcept : status_(status) {}

    [[nodiscard]] constexpr bool ok() const noexcept { return status_ == Status::Ok; }
    [[nodiscard]] constexpr Status status() const noexcept { return status_; }

private:
    Status status_;
};

// Emits low-latency CPU pause instruction in busy-spin loops
SYNAPSE_FORCE_INLINE void cpu_relax() noexcept {
#if defined(_MSC_VER) || defined(__i386__) || defined(__x86_64__)
    _mm_pause();
#elif defined(__aarch64__) || defined(__arm__)
    asm volatile("yield" ::: "memory");
#else
    std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
}

// Monotonic nanosecond timestamp
[[nodiscard]] SYNAPSE_FORCE_INLINE uint64_t get_timestamp_ns() noexcept {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count()
    );
}

} // namespace synapse
