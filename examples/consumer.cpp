#include "synapse/synapse.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>

#pragma pack(push, 1)
struct MarketTick {
    uint64_t symbol_id;
    double bid_price;
    double ask_price;
    uint32_t bid_volume;
    uint32_t ask_volume;
    uint64_t engine_timestamp;
};
#pragma pack(pop)

int main() {
    std::cout << "[Consumer] Attaching to Synapse channel 'synapse://market-feed'...\n";

    auto sub_res = synapse::Subscriber::attach("market-feed");
    if (!sub_res.ok()) {
        std::cerr << "[Consumer] Failed to attach to channel: "
                  << synapse::status_to_string(sub_res.status()) << "\n";
        return 1;
    }

    auto sub = std::move(sub_res.value());
    std::cout << "[Consumer] Attached. Processing zero-copy stream...\n";

    uint64_t total_messages = 0;
    uint64_t total_latency_ns = 0;
    auto last_report = std::chrono::steady_clock::now();

    while (true) {
        sub.wait_and_poll([&](const synapse::MessageHeader& hdr, std::span<const uint8_t> payload) {
            const uint64_t now_ns = synapse::get_timestamp_ns();
            if (payload.size() >= sizeof(MarketTick)) {
                const auto* tick = reinterpret_cast<const MarketTick*>(payload.data());
                if (now_ns >= tick->engine_timestamp) {
                    total_latency_ns += (now_ns - tick->engine_timestamp);
                }
            }
            ++total_messages;
        }, std::chrono::milliseconds(50));

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed_sec = std::chrono::duration<double>(now - last_report).count();
        if (elapsed_sec >= 1.0 && total_messages > 0) {
            const double avg_lat_ns = static_cast<double>(total_latency_ns) / static_cast<double>(total_messages);
            const double msg_rate = static_cast<double>(total_messages) / elapsed_sec;

            std::cout << "[Consumer] Received: " << std::setw(9) << total_messages
                      << " | Rate: " << std::fixed << std::setprecision(2) << (msg_rate / 1'000'000.0) << " M/s"
                      << " | Avg Latency: " << std::setprecision(1) << avg_lat_ns << " ns\n";

            total_messages = 0;
            total_latency_ns = 0;
            last_report = now;
        }
    }

    return 0;
}
