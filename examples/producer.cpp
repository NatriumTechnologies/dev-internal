#include "synapse/synapse.hpp"
#include <iostream>
#include <thread>
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
    std::cout << "[Producer] Initializing Synapse channel 'synapse://market-feed'...\n";

    auto pub_res = synapse::Publisher::create("market-feed", 4 * 1024 * 1024, sizeof(MarketTick));
    if (!pub_res.ok()) {
        std::cerr << "[Producer] Failed to create publisher: "
                  << synapse::status_to_string(pub_res.status()) << "\n";
        return 1;
    }

    auto pub = std::move(pub_res.value());
    std::cout << "[Producer] Channel mapped. Streaming ticks...\n";

    MarketTick tick{};
    tick.symbol_id = 0xDEADBEEFCAFE0001ULL;
    tick.bid_price = 4520.25;
    tick.ask_price = 4520.50;
    tick.bid_volume = 150;
    tick.ask_volume = 320;

    uint64_t counter = 0;
    const auto start_time = std::chrono::steady_clock::now();

    while (true) {
        tick.engine_timestamp = synapse::get_timestamp_ns();
        tick.bid_price += 0.05 * ((counter % 3 == 0) ? 1.0 : -0.5);
        tick.ask_price = tick.bid_price + 0.25;

        auto status = pub.send(0x1001, &tick, sizeof(tick));
        if (SYNAPSE_UNLIKELY(!status.ok())) {
            synapse::cpu_relax();
            continue;
        }

        ++counter;
        if (SYNAPSE_UNLIKELY(counter % 1'000'000 == 0)) {
            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - start_time).count();
            std::cout << "[Producer] Sent " << counter << " ticks ("
                      << (static_cast<double>(counter) / elapsed / 1'000'000.0)
                      << " M/sec)\n";
        }
    }

    return 0;
}
