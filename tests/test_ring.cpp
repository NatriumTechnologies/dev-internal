#include "synapse/synapse.hpp"
#include <iostream>
#include <vector>
#include <thread>
#include <cassert>

using namespace synapse;

void test_crc32() {
    std::cout << "[TEST] Running test_crc32...\n";
    const char* sample = "123456789";
    const uint32_t expected = 0xCBF43926u; // Standard IEEE 802.3 check value
    const uint32_t actual = Crc32::calculate(sample, 9);
    assert(actual == expected && "CRC-32 vector failed");
    std::cout << "  -> CRC-32 check value verified (0xCBF43926)\n";
}

void test_basic_ring() {
    std::cout << "[TEST] Running test_basic_ring...\n";
    constexpr size_t kCap = 4096;
    alignas(kCacheLineSize) AtomicRingBuffer::ControlBlock ctrl{};
    ctrl.capacity = kCap;
    std::vector<uint8_t> storage(kCap, 0);

    AtomicRingBuffer ring(&ctrl, storage.data(), kCap);
    assert(ring.capacity() == kCap);
    assert(ring.available_write() == kCap);
    assert(ring.available_read() == 0);

    const char* msg = "Synapse ring buffer test";
    const size_t len = std::strlen(msg) + 1;

    uint8_t* slot = ring.claim(len);
    assert(slot != nullptr);
    std::memcpy(slot, msg, len);
    ring.commit(len);

    assert(ring.available_read() > 0);

    size_t out_len = 0;
    const uint8_t* read_ptr = ring.peek(out_len);
    assert(read_ptr != nullptr);
    assert(out_len == len);
    assert(std::strcmp(reinterpret_cast<const char*>(read_ptr), msg) == 0);

    ring.consume(out_len);
    assert(ring.available_read() == 0);
    std::cout << "  -> Basic ring claim/commit/peek/consume passed\n";
}

void test_ring_wrap_around() {
    std::cout << "[TEST] Running test_ring_wrap_around...\n";
    constexpr size_t kCap = 1024;
    alignas(kCacheLineSize) AtomicRingBuffer::ControlBlock ctrl{};
    ctrl.capacity = kCap;
    std::vector<uint8_t> storage(kCap, 0);

    AtomicRingBuffer ring(&ctrl, storage.data(), kCap);

    // Fill buffer in increments of 128 bytes until it wraps around multiple times
    constexpr size_t kChunkSize = 120;
    uint8_t send_buf[kChunkSize];

    for (uint32_t iter = 0; iter < 100; ++iter) {
        std::memset(send_buf, static_cast<uint8_t>(iter), sizeof(send_buf));
        uint8_t* slot = ring.claim(sizeof(send_buf));
        assert(slot != nullptr);
        std::memcpy(slot, send_buf, sizeof(send_buf));
        ring.commit(sizeof(send_buf));

        size_t read_len = 0;
        const uint8_t* read_ptr = ring.peek(read_len);
        assert(read_ptr != nullptr);
        assert(read_len == sizeof(send_buf));
        assert(read_ptr[0] == static_cast<uint8_t>(iter));
        ring.consume(read_len);
    }

    std::cout << "  -> Wrap-around boundary handling passed across 100 cycles\n";
}

void test_cross_thread_spsc() {
    std::cout << "[TEST] Running test_cross_thread_spsc...\n";
    auto pub_res = Publisher::create("synapse_test_spsc", 1024 * 1024, 64);
    assert(pub_res.ok());
    auto sub_res = Subscriber::attach("synapse_test_spsc");
    assert(sub_res.ok());

    auto pub = std::move(pub_res.value());
    auto sub = std::move(sub_res.value());

    constexpr uint32_t kTotalMessages = 100'000;
    std::atomic<bool> producer_done{false};
    std::atomic<uint32_t> received_count{0};

    std::thread consumer_thread([&]() {
        uint32_t expected_seq = 1;
        while (!producer_done.load(std::memory_order_relaxed) || received_count.load() < kTotalMessages) {
            sub.poll([&](const MessageHeader& hdr, std::span<const uint8_t> payload) {
                assert(hdr.sequence == expected_seq && "Sequence ordering violated");
                assert(payload.size() == sizeof(uint32_t));
                const uint32_t val = *reinterpret_cast<const uint32_t*>(payload.data());
                assert(val == expected_seq);
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

    producer_done.store(true, std::memory_order_release);
    consumer_thread.join();

    assert(received_count.load() == kTotalMessages);
    std::cout << "  -> SPSC concurrent delivery verified (100,000 ordered frames)\n";
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
