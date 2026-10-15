#include "synapse/channel.hpp"
#include <bit>
#include <new>

namespace synapse {

namespace {

inline size_t round_up_pow2(size_t v) noexcept {
    if (v < 1024) v = 1024;
    return std::bit_ceil(v);
}

} // namespace

Result<Publisher> Publisher::create(
    std::string_view channel_name,
    size_t ring_capacity,
    size_t max_payload_size
) {
    const size_t capacity = round_up_pow2(ring_capacity);
    const size_t total_size = sizeof(ChannelLayout) + capacity;

    auto shm_res = SharedMemory::open(channel_name, total_size, ShmAccess::CreateOrOpen);
    if (!shm_res.ok()) {
        return shm_res.status();
    }

    SharedMemory shm = std::move(shm_res.value());
    auto* layout = static_cast<ChannelLayout*>(shm.data());

    if (shm.is_creator()) {
        new (layout) ChannelLayout();
        layout->magic = kProtocolMagic;
        layout->version = kProtocolVersion;
        layout->total_shm_size = total_size;
        layout->ring_capacity = capacity;
        layout->max_payload_size = max_payload_size;
        layout->ring_control.capacity = capacity;
    } else {
        if (layout->magic != kProtocolMagic) {
            return Status::InvalidMagic;
        }
        if (layout->version != kProtocolVersion) {
            return Status::VersionMismatch;
        }
    }

    auto* buffer_start = reinterpret_cast<uint8_t*>(shm.data()) + sizeof(ChannelLayout);
    auto ring = std::make_unique<AtomicRingBuffer>(
        &layout->ring_control,
        buffer_start,
        layout->ring_capacity
    );

    Publisher pub;
    pub.shm_ = std::move(shm);
    pub.layout_ = layout;
    pub.ring_ = std::move(ring);
    pub.next_sequence_ = 1;

    return pub;
}

size_t Publisher::available_write() noexcept {
    return ring_ ? ring_->available_write() : 0;
}

Result<void> Publisher::send(
    uint16_t msg_type,
    const void* payload,
    size_t payload_len,
    uint32_t flags
) {
    if (SYNAPSE_UNLIKELY(!ring_ || !layout_)) {
        return Status::ChannelClosed;
    }

    if (SYNAPSE_UNLIKELY(payload_len > layout_->max_payload_size)) {
        return Status::PayloadTooLarge;
    }

    const size_t message_size = sizeof(MessageHeader) + payload_len;
    uint8_t* slot = ring_->claim(message_size);
    if (SYNAPSE_UNLIKELY(!slot)) {
        return Status::BufferFull;
    }

    auto* hdr = reinterpret_cast<MessageHeader*>(slot);
    hdr->magic = kProtocolMagic;
    hdr->version = kProtocolVersion;
    hdr->msg_type = msg_type;
    hdr->sequence = next_sequence_++;
    hdr->payload_len = static_cast<uint32_t>(payload_len);
    hdr->timestamp_ns = get_timestamp_ns();
    hdr->flags = flags;

    if (payload && payload_len > 0) {
        hdr->crc32 = Crc32::calculate(payload, payload_len);
        std::memcpy(slot + sizeof(MessageHeader), payload, payload_len);
    } else {
        hdr->crc32 = 0;
    }

    ring_->commit(message_size);

    // Bump notification word and wake waiting consumers
    ring_->notify_word().fetch_add(1, std::memory_order_release);
    Futex::wake_one(ring_->notify_word());

    return Status::Ok;
}

Result<Subscriber> Subscriber::attach(
    std::string_view channel_name,
    size_t expected_size
) {
    const size_t probe_size = (expected_size > 0) ? expected_size : (sizeof(ChannelLayout) + 1024 * 1024);
    auto shm_res = SharedMemory::open(channel_name, probe_size, ShmAccess::OpenExisting);
    if (!shm_res.ok()) {
        return shm_res.status();
    }

    SharedMemory shm = std::move(shm_res.value());
    auto* layout = static_cast<ChannelLayout*>(shm.data());

    if (layout->magic != kProtocolMagic) {
        return Status::InvalidMagic;
    }
    if (layout->version != kProtocolVersion) {
        return Status::VersionMismatch;
    }

    auto* buffer_start = reinterpret_cast<uint8_t*>(shm.data()) + sizeof(ChannelLayout);
    auto ring = std::make_unique<AtomicRingBuffer>(
        &layout->ring_control,
        buffer_start,
        layout->ring_capacity
    );

    Subscriber sub;
    sub.shm_ = std::move(shm);
    sub.layout_ = layout;
    sub.last_observed_futex_ = ring->notify_word().load(std::memory_order_relaxed);
    sub.ring_ = std::move(ring);

    return sub;
}

size_t Subscriber::poll(const MessageCallback& callback) {
    if (SYNAPSE_UNLIKELY(!ring_ || !layout_)) {
        return 0;
    }

    size_t processed = 0;
    while (true) {
        size_t slot_size = 0;
        const uint8_t* slot = ring_->peek(slot_size);
        if (!slot || slot_size < sizeof(MessageHeader)) {
            break;
        }

        const auto* hdr = reinterpret_cast<const MessageHeader*>(slot);
        if (SYNAPSE_LIKELY(validate_header(*hdr, layout_->max_payload_size))) {
            const auto* payload_ptr = slot + sizeof(MessageHeader);
            const size_t payload_len = hdr->payload_len;

            bool valid = true;
            if (payload_len > 0) {
                const uint32_t expected_crc = Crc32::calculate(payload_ptr, payload_len);
                if (SYNAPSE_UNLIKELY(expected_crc != hdr->crc32)) {
                    valid = false;
                }
            }

            if (SYNAPSE_LIKELY(valid)) {
                callback(*hdr, std::span<const uint8_t>(payload_ptr, payload_len));
            }
        }

        ring_->consume(slot_size);
        ++processed;
    }

    return processed;
}

Result<size_t> Subscriber::wait_and_poll(
    const MessageCallback& callback,
    std::chrono::nanoseconds timeout
) {
    const size_t immediate = poll(callback);
    if (immediate > 0) {
        return immediate;
    }

    const uint32_t current_futex = ring_->notify_word().load(std::memory_order_relaxed);
    last_observed_futex_ = current_futex;

    (void)Futex::wait(ring_->notify_word(), current_futex, timeout);
    const size_t after_wait = poll(callback);
    return after_wait;
}

} // namespace synapse
