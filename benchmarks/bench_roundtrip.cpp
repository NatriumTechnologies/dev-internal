#include "synapse/synapse.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <numeric>
#include <thread>
#include <chrono>

using namespace synapse;

int main(int argc, char* argv[]) {
    std::cout << "========================================================\n";
    std::cout << " Synapse IPC - Cacheline-Aligned Roundtrip Benchmark\n";
    std::cout << "========================================================\n\n";

    constexpr size_t kIterations = 200'000;
    constexpr size_t kPayloadSize = 64; // Standard 64-byte telemetry tick

    std::cout << "Configuring channels...\n";
    std::cout << " - Payload size: " << kPayloadSize << " bytes (+32B header = 96B total)\n";
    std::cout << " - Iterations:   " << kIterations << "\n\n";

    auto pub_a_res = Publisher::create("synapse_bench_a2b", 1024 * 1024, 4096);
    auto pub_b_res = Publisher::create("synapse_bench_b2a", 1024 * 1024, 4096);

    if (!pub_a_res.ok() || !pub_b_res.ok()) {
        std::cerr << "Failed to initialize benchmark channels.\n";
        return 1;
    }

    auto sub_b_res = Subscriber::attach("synapse_bench_a2b");
    auto sub_a_res = Subscriber::attach("synapse_bench_b2a");

    if (!sub_b_res.ok() || !sub_a_res.ok()) {
        std::cerr << "Failed to attach subscriber channels.\n";
        return 1;
    }

    Publisher pub_a = std::move(pub_a_res.value());
    Publisher pub_b = std::move(pub_b_res.value());
    Subscriber sub_b = std::move(sub_b_res.value());
    Subscriber sub_a = std::move(sub_a_res.value());

    std::atomic<bool> running{true};
    std::vector<uint64_t> latencies_ns;
    latencies_ns.reserve(kIterations);

    // Echo thread (simulating distinct process/core B)
    std::thread echo_thread([&]() {
        uint8_t echo_buf[kPayloadSize];
        while (running.load(std::memory_order_relaxed)) {
            sub_b.poll([&](const MessageHeader& hdr, std::span<const uint8_t> payload) {
                pub_b.send(hdr.msg_type, payload.data(), payload.size());
            });
            cpu_relax();
        }
    });

    uint8_t payload[kPayloadSize];
    std::memset(payload, 0xAB, sizeof(payload));

    std::cout << "Warming up CPU caches...\n";
    for (size_t i = 0; i < 5000; ++i) {
        pub_a.send(1, payload, sizeof(payload));
        while (sub_a.poll([](const auto&, auto) {}) == 0) {
            cpu_relax();
        }
    }

    std::cout << "Benchmarking bidirectional roundtrip...\n";
    const auto t_start = std::chrono::steady_clock::now();

    for (size_t i = 0; i < kIterations; ++i) {
        const uint64_t t0 = get_timestamp_ns();
        pub_a.send(100, payload, sizeof(payload));

        bool received = false;
        while (!received) {
            sub_a.poll([&](const MessageHeader&, auto) {
                const uint64_t t1 = get_timestamp_ns();
                latencies_ns.push_back(t1 - t0);
                received = true;
            });
            cpu_relax();
        }
    }

    const auto t_end = std::chrono::steady_clock::now();
    running.store(false, std::memory_order_relaxed);
    echo_thread.join();

    const double total_sec = std::chrono::duration<double>(t_end - t_start).count();
    const double msg_rate = (static_cast<double>(kIterations) * 2.0) / total_sec;
    const double throughput_mb = (msg_rate * static_cast<double>(kPayloadSize + sizeof(MessageHeader))) / (1024.0 * 1024.0);

    std::sort(latencies_ns.begin(), latencies_ns.end());

    const auto min_lat = latencies_ns.front();
    const auto p50_lat = latencies_ns[static_cast<size_t>(latencies_ns.size() * 0.50)];
    const auto p90_lat = latencies_ns[static_cast<size_t>(latencies_ns.size() * 0.90)];
    const auto p99_lat = latencies_ns[static_cast<size_t>(latencies_ns.size() * 0.99)];
    const auto p999_lat = latencies_ns[static_cast<size_t>(latencies_ns.size() * 0.999)];
    const auto max_lat = latencies_ns.back();

    const double sum = std::accumulate(latencies_ns.begin(), latencies_ns.end(), 0.0);
    const double mean_lat = sum / static_cast<double>(latencies_ns.size());

    std::cout << "\n--------------------------------------------------------\n";
    std::cout << " Latency Percentiles (Roundtrip Ping-Pong)\n";
    std::cout << "--------------------------------------------------------\n";
    std::cout << std::fixed << std::setprecision(1);
    std::cout << " Min:      " << std::setw(8) << min_lat << " ns\n";
    std::cout << " 50.0th:   " << std::setw(8) << p50_lat << " ns\n";
    std::cout << " 90.0th:   " << std::setw(8) << p90_lat << " ns\n";
    std::cout << " 99.0th:   " << std::setw(8) << p99_lat << " ns\n";
    std::cout << " 99.9th:   " << std::setw(8) << p999_lat << " ns\n";
    std::cout << " Max:      " << std::setw(8) << max_lat << " ns\n";
    std::cout << " Mean:     " << std::setw(8) << mean_lat << " ns\n";
    std::cout << "--------------------------------------------------------\n";
    std::cout << " Total Time:      " << std::setprecision(3) << total_sec << " s\n";
    std::cout << " Message Rate:    " << std::setprecision(1) << (msg_rate / 1'000'000.0) << " M msgs/sec\n";
    std::cout << " Raw Bandwidth:   " << std::setprecision(2) << throughput_mb << " MB/sec\n";
    std::cout << "========================================================\n";

    return 0;
}
