#pragma once

#include "synapse/common.hpp"
#include <cstring>
#include <array>

namespace synapse {

inline constexpr uint32_t kProtocolMagic = 0x53594E50; // ASCII 'SYNP'
inline constexpr uint16_t kProtocolVersion = 1;

enum MessageFlags : uint32_t {
    kFlagNone       = 0x00000000,
    kFlagCompressed = 0x00000001,
    kFlagUrgent     = 0x00000002,
    kFlagHeartbeat  = 0x00000004,
    kFlagFragmented = 0x00000008
};

#pragma pack(push, 1)
struct MessageHeader {
    uint32_t magic;         // 0x53594E50 ('SYNP')
    uint16_t version;       // Protocol version (1)
    uint16_t msg_type;      // Application-defined message category
    uint32_t sequence;      // Monotonically increasing sequence ID
    uint32_t payload_len;   // Byte length of payload immediately following header
    uint64_t timestamp_ns;  // Monotonic sender timestamp in nanoseconds
    uint32_t crc32;         // IEEE 802.3 CRC-32 checksum of payload
    uint32_t flags;         // MessageFlags bitmask
};
#pragma pack(pop)

static_assert(sizeof(MessageHeader) == 32, "MessageHeader must be exactly 32 bytes");

class Crc32 {
public:
    static constexpr uint32_t kPolynomial = 0xEDB88320u;

    static consteval auto generate_table() noexcept {
        std::array<uint32_t, 256> table{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t crc = i;
            for (uint32_t j = 0; j < 8; ++j) {
                if (crc & 1) {
                    crc = (crc >> 1) ^ kPolynomial;
                } else {
                    crc >>= 1;
                }
            }
            table[i] = crc;
        }
        return table;
    }

    [[nodiscard]] static uint32_t calculate(const void* data, size_t length, uint32_t seed = 0xFFFFFFFFu) noexcept {
        static constexpr auto kTable = generate_table();
        const auto* byte_ptr = static_cast<const uint8_t*>(data);
        uint32_t crc = seed;

        for (size_t i = 0; i < length; ++i) {
            const uint8_t lookup_idx = static_cast<uint8_t>(crc ^ byte_ptr[i]);
            crc = (crc >> 8) ^ kTable[lookup_idx];
        }

        return crc ^ 0xFFFFFFFFu;
    }
};

[[nodiscard]] SYNAPSE_FORCE_INLINE bool validate_header(
    const MessageHeader& hdr,
    size_t max_payload_len
) noexcept {
    if (SYNAPSE_UNLIKELY(hdr.magic != kProtocolMagic)) {
        return false;
    }
    if (SYNAPSE_UNLIKELY(hdr.version != kProtocolVersion)) {
        return false;
    }
    if (SYNAPSE_UNLIKELY(hdr.payload_len > max_payload_len)) {
        return false;
    }
    return true;
}

} // namespace synapse
