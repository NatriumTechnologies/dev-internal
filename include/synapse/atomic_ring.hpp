#pragma once

#include "synapse/common.hpp"
#include <atomic>
#include <cstring>
#include <cassert>
#include <span>

namespace synapse {

inline constexpr uint32_t kRingWrapMarker = 0xFFFFFFFFu;

// Cacheline-isolated Single-Producer Single-Consumer lock-free ring buffer
class AtomicRingBuffer {
public:
    struct alignas(kCacheLineSize) ControlBlock {
        alignas(kCacheLineSize) std::atomic<uint64_t> write_index{0};
        alignas(kCacheLineSize) std::atomic<uint64_t> read_index{0};
        alignas(kCacheLineSize) std::atomic<uint32_t> notify_word{0};
        alignas(kCacheLineSize) uint64_t capacity{0};
    };

    AtomicRingBuffer(ControlBlock* control, uint8_t* buffer, size_t capacity) noexcept
        : control_(control),
          buffer_(buffer),
          capacity_(capacity),
          cached_read_index_(0),
          cached_write_index_(0) {
        assert(control_ != nullptr);
        assert(buffer_ != nullptr);
        assert((capacity & (capacity - 1)) == 0 && "Capacity must be a power of two");
    }

    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }

    [[nodiscard]] uint64_t write_index() const noexcept {
        return control_->write_index.load(std::memory_order_relaxed);
    }

    [[nodiscard]] uint64_t read_index() const noexcept {
        return control_->read_index.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::atomic<uint32_t>& notify_word() noexcept {
        return control_->notify_word;
    }

    // Producer side: Available bytes to write before overwriting consumer
    [[nodiscard]] size_t available_write() noexcept {
        const uint64_t current_write = control_->write_index.load(std::memory_order_relaxed);
        const uint64_t current_read = cached_read_index_;
        size_t consumed = static_cast<size_t>(current_write - current_read);

        if (consumed >= capacity_) {
            // Refresh cached read cursor from shared memory
            cached_read_index_ = control_->read_index.load(std::memory_order_acquire);
            consumed = static_cast<size_t>(current_write - cached_read_index_);
            if (consumed >= capacity_) {
                return 0;
            }
        }
        return capacity_ - consumed;
    }

    // Producer side: Zero-copy reservation of contiguous byte slot
    [[nodiscard]] uint8_t* claim(size_t size) noexcept {
        const size_t total_needed = sizeof(uint32_t) + size;
        const size_t avail = available_write();

        if (SYNAPSE_UNLIKELY(avail < total_needed)) {
            return nullptr;
        }

        const uint64_t write_idx = control_->write_index.load(std::memory_order_relaxed);
        const size_t offset = static_cast<size_t>(write_idx & (capacity_ - 1));
        const size_t space_to_end = capacity_ - offset;

        // If contiguous space to end of ring is too small, inject a wrap marker
        if (SYNAPSE_UNLIKELY(space_to_end < total_needed)) {
            if (avail < (space_to_end + total_needed)) {
                return nullptr;
            }

            // Write wrap marker into remaining trailing space
            auto* marker = reinterpret_cast<uint32_t*>(buffer_ + offset);
            *marker = kRingWrapMarker;

            // Advance write index to wrap boundary
            control_->write_index.store(write_idx + space_to_end, std::memory_order_release);
            
            // Allocate at offset 0
            auto* slot_len_ptr = reinterpret_cast<uint32_t*>(buffer_);
            *slot_len_ptr = static_cast<uint32_t>(size);
            return buffer_ + sizeof(uint32_t);
        }

        auto* slot_len_ptr = reinterpret_cast<uint32_t*>(buffer_ + offset);
        *slot_len_ptr = static_cast<uint32_t>(size);
        return buffer_ + offset + sizeof(uint32_t);
    }

    // Producer side: Commit claimed reservation
    void commit(size_t size) noexcept {
        const size_t total_written = sizeof(uint32_t) + size;
        const uint64_t current_write = control_->write_index.load(std::memory_order_relaxed);
        control_->write_index.store(current_write + total_written, std::memory_order_release);
    }

    // Consumer side: Available bytes to read
    [[nodiscard]] size_t available_read() noexcept {
        const uint64_t current_read = control_->read_index.load(std::memory_order_relaxed);
        const uint64_t current_write = cached_write_index_;

        if (current_read == current_write) {
            cached_write_index_ = control_->write_index.load(std::memory_order_acquire);
            if (current_read == cached_write_index_) {
                return 0;
            }
        }
        return static_cast<size_t>(cached_write_index_ - current_read);
    }

    // Consumer side: Peek next contiguous slot without copying
    [[nodiscard]] const uint8_t* peek(size_t& out_size) noexcept {
        if (available_read() == 0) {
            out_size = 0;
            return nullptr;
        }

        uint64_t read_idx = control_->read_index.load(std::memory_order_relaxed);
        size_t offset = static_cast<size_t>(read_idx & (capacity_ - 1));

        const auto* slot_header = reinterpret_cast<const uint32_t*>(buffer_ + offset);
        if (SYNAPSE_UNLIKELY(*slot_header == kRingWrapMarker)) {
            // Skip wrap padding and reset to start of buffer
            const size_t space_to_end = capacity_ - offset;
            read_idx += space_to_end;
            control_->read_index.store(read_idx, std::memory_order_release);
            
            if (available_read() == 0) {
                out_size = 0;
                return nullptr;
            }
            offset = 0;
            slot_header = reinterpret_cast<const uint32_t*>(buffer_);
        }

        out_size = *slot_header;
        return buffer_ + offset + sizeof(uint32_t);
    }

    // Consumer side: Release consumed slot
    void consume(size_t size) noexcept {
        const size_t total_consumed = sizeof(uint32_t) + size;
        const uint64_t current_read = control_->read_index.load(std::memory_order_relaxed);
        control_->read_index.store(current_read + total_consumed, std::memory_order_release);
    }

private:
    ControlBlock* control_;
    uint8_t* buffer_;
    size_t capacity_;
    alignas(kCacheLineSize) uint64_t cached_read_index_;
    alignas(kCacheLineSize) uint64_t cached_write_index_;
};

} // namespace synapse
