#pragma once

#include "synapse/common.hpp"
#include "synapse/shm.hpp"
#include "synapse/futex.hpp"
#include "synapse/protocol.hpp"
#include "synapse/atomic_ring.hpp"
#include <functional>
#include <span>

namespace synapse {

struct alignas(kCacheLineSize) ChannelLayout {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint64_t total_shm_size;
    uint64_t ring_capacity;
    uint64_t max_payload_size;
    alignas(kCacheLineSize) AtomicRingBuffer::ControlBlock ring_control;
};

using MessageCallback = std::function<void(const MessageHeader& header, std::span<const uint8_t> payload)>;

class Publisher {
public:
    Publisher() = default;
    ~Publisher() = default;

    Publisher(Publisher&&) noexcept = default;
    Publisher& operator=(Publisher&&) noexcept = default;

    [[nodiscard]] static Result<Publisher> create(
        std::string_view channel_name,
        size_t ring_capacity = (1024 * 1024), // 1MB default
        size_t max_payload_size = 65536
    );

    [[nodiscard]] Result<void> send(
        uint16_t msg_type,
        const void* payload,
        size_t payload_len,
        uint32_t flags = kFlagNone
    );

    [[nodiscard]] size_t available_write() noexcept;

private:
    SharedMemory shm_;
    ChannelLayout* layout_{nullptr};
    std::unique_ptr<AtomicRingBuffer> ring_;
    uint32_t next_sequence_{1};
};

class Subscriber {
public:
    Subscriber() = default;
    ~Subscriber() = default;

    Subscriber(Subscriber&&) noexcept = default;
    Subscriber& operator=(Subscriber&&) noexcept = default;

    [[nodiscard]] static Result<Subscriber> attach(
        std::string_view channel_name,
        size_t expected_size = 0
    );

    // Non-blocking poll: processes all currently available messages
    size_t poll(const MessageCallback& callback);

    // Blocking wait with adaptive spin & futex: processes incoming messages
    Result<size_t> wait_and_poll(
        const MessageCallback& callback,
        std::chrono::nanoseconds timeout = std::chrono::milliseconds(100)
    );

private:
    SharedMemory shm_;
    ChannelLayout* layout_{nullptr};
    std::unique_ptr<AtomicRingBuffer> ring_;
    uint32_t last_observed_futex_{0};
};

} // namespace synapse
