#include "synapse/synapse.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <cstdlib>

using namespace synapse;

#define SYNAPSE_TEST_ASSERT(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "[FAIL] Assertion failed at " << __FILE__ << ":" << __LINE__ << " -> " #expr "\n"; \
            std::abort(); \
        } \
    } while (0)

void test_crc32() {
    std::cout << "[TEST] Running test_crc32...\n";
    const char* sample = "123456789";
    const uint32_t expected = 0xCBF43926u; // Standard IEEE 802.3 check value
    const uint32_t actual = Crc32::calculate(sample, 9);
    SYNAPSE_TEST_ASSERT(actual == expected);
    std::cout << "  -> CRC-32 check value verified (0xCBF43926)\n";
}

void test_basic_ring() {
    std::cout << "[TEST] Running test_basic_ring...\n";
    constexpr size_t kCap = 4096;
    alignas(kCacheLineSize) AtomicRingBuffer::ControlBlock ctrl{};
    ctrl.capacity = kCap;
    std::vector<uint8_t> storage(kCap, 0);

    AtomicRingBuffer ring(&ctrl, storage.data(), kCap);
    SYNAPSE_TEST_ASSERT(ring.capacity() == kCap);
    SYNAPSE_TEST_ASSERT(ring.available_write() == kCap);
    SYNAPSE_TEST_ASSERT(ring.available_read() == 0);

    const char* msg = "Synapse ring buffer test";
    const size_t len = std::strlen(msg) + 1;

    uint8_t* slot = ring.claim(len);
    SYNAPSE_TEST_ASSERT(slot != nullptr);
    std::memcpy(slot, msg, len);
    ring.commit(len);

    SYNAPSE_TEST_ASSERT(ring.available_read() > 0);

    size_t out_len = 0;
    const uint8_t* read_ptr = ring.peek(out_len);
    SYNAPSE_TEST_ASSERT(read_ptr != nullptr);
    SYNAPSE_TEST_ASSERT(out_len == len);
    SYNAPSE_TEST_ASSERT(std::strcmp(reinterpret_cast<const char*>(read_ptr), msg) == 0);

    ring.consume(out_len);
    SYNAPSE_TEST_ASSERT(ring.available_read() == 0);
    std::cout << "  -> Basic ring claim/commit/peek/consume passed\n";
}

void test_ring_wrap_around() {
    std::cout << "[TEST] Running test_ring_wrap_around...\n";
    constexpr size_t kCap = 1024;
    alignas(kCacheLineSize) AtomicRingBuffer::ControlBlock ctrl{};
    ctrl.capacity = kCap;
    std::vector<uint8_t> storage(kCap, 0);

    AtomicRingBuffer ring(&ctrl, storage.data(), kCap);

    constexpr size_t kChunkSize = 120;
    uint8_t send_buf[kChunkSize];

    for (uint32_t iter = 0; iter < 100; ++iter) {
        std::memset(send_buf, static_cast<uint8_t>(iter), sizeof(send_buf));
        uint8_t* slot = ring.claim(sizeof(send_buf));
        SYNAPSE_TEST_ASSERT(slot != nullptr);
        std::memcpy(slot, send_buf, sizeof(send_buf));
        ring.commit(sizeof(send_buf));

        size_t read_len = 0;
        const uint8_t* read_ptr = ring.peek(read_len);
        SYNAPSE_TEST_ASSERT(read_ptr != nullptr);
        SYNAPSE_TEST_ASSERT(read_len == sizeof(send_buf));
        SYNAPSE_TEST_ASSERT(read_ptr[0] == static_cast<uint8_t>(iter));
        ring.consume(read_len);
    }

    std::cout << "  -> Wrap-around boundary handling passed across 100 cycles\n";
}

void test_cross_thread_spsc() {
    std::cout << "[TEST] Running test_cross_thread_spsc...\n";
    auto pub_res = Publisher::create("synapse_test_spsc", 1024 * 1024, 64);
    SYNAPSE_TEST_ASSERT(pub_res.ok());
    auto sub_res = Subscriber::attach("synapse_test_spsc");
    SYNAPSE_TEST_ASSERT(sub_res.ok());

    auto pub = std::move(pub_res.value());
    auto sub = std::move(sub_res.value());

    constexpr uint32_t kTotalMessages = 20'000;
    std::atomic<uint32_t> received_count{0};

    std::thread consumer_thread([&]() {
        uint32_t expected_seq = 1;
        while (received_count.load(std::memory_order_relaxed) < kTotalMessages) {
            sub.poll([&](const MessageHeader& hdr, std::span<const uint8_t> payload) {
                SYNAPSE_TEST_ASSERT(hdr.sequence == expected_seq);
                SYNAPSE_TEST_ASSERT(payload.size() == sizeof(uint32_t));
                const uint32_t val = *reinterpret_cast<const uint32_t*>(payload.data());
                SYNAPSE_TEST_ASSERT(val == expected_seq);
                ++expected_seq;
                received_count.fetch_add(1, std::memory_order_relaxed);
            });
            cpu_relax();
        }
    });

    for (uint32_t i = 1; i <= kTotalMessages; ++i) {
        while (!pub.send(1, &i, sizeof(uint32_t)).ok()) {
            cpu_relax();
        }
    }

    consumer_thread.join();

    SYNAPSE_TEST_ASSERT(received_count.load() == kTotalMessages);
    std::cout << "  -> SPSC concurrent delivery verified (20,000 ordered frames)\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << " Synapse IPC Unit & Concurrency Tests   \n";
    std::cout << "========================================\n";

    test_crc32();
    test_basic_ring();
    test_ring_wrap_around();
    test_cross_thread_spsc();

    std::cout << "\nALL SYNAPSE IPC TESTS PASSED SUCCESSFULLY.\n";
    return 0;
}
