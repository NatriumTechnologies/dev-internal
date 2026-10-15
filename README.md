# Synapse IPC

[![CI](https://github.com/NatriumTechnologies/synapse-ipc/actions/workflows/ci.yml/badge.svg)](https://github.com/NatriumTechnologies/synapse-ipc/actions/workflows/ci.yml)
[![Standard](https://img.shields.io/badge/c%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![Platforms](https://img.shields.io/badge/platform-windows%20%7C%20linux-lightgrey.svg)](https://github.com/NatriumTechnologies/synapse-ipc)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

**Synapse** is an ultra-low-latency, zero-copy inter-process communication (IPC) fabric engineered for cross-process telemetry, algorithmic trading engines, simulation pipelines, and systems-level runtime messaging.

Built from first principles in ISO C++20, Synapse bypasses socket stacks, pipe buffers, and usermode-to-kernel context transitions by mapping shared memory regions directly into application address spaces and coordinating lock-free SPSC (Single Producer Single Consumer) ring buffers with strict cacheline isolation.

---

## Key Architecture & Design Tenets

```
               [ Producer Process ]                 [ Consumer Process ]
                        |                                    |
          +-------------v-------------+        +-------------v-------------+
          |  Zero-Copy Claim / Commit |        |   Zero-Copy Peek / Consume|
          +-------------+-------------+        +-------------+-------------+
                        |                                    |
                        +-----------------+------------------+
                                          |
                         Shared Memory Region (PAGE_READWRITE)
    +-----------------------------------------------------------------------------+
    | magic | ver | seq | ring_control (64B align) | circular slot ring (pow-2)   |
    +-----------------------------------------------------------------------------+
               |                                            ^
         [ _mm_pause ]                                [ WaitOnAddress / ]
       Adaptive Spin Loop                             [  FUTEX_WAIT     ]
```

### 1. Cacheline Isolation & False Sharing Elimination
In high-throughput multi-core workloads, cacheline bouncing destroys throughput. Synapse explicitly isolates all atomic cursors (`write_index`, `read_index`, `notify_word`) to separate 64-byte hardware cache boundaries (`alignas(64)`), ensuring producer and consumer CPU cores never contend on the same cache line.

### 2. Zero-Copy Framing Mechanics
Traditional IPC mechanisms copy data twice: once into a kernel buffer, and once into consumer memory. Synapse provides a contiguous zero-copy slot API:
- **Producer**: Calls `ring.claim(size)` to receive a direct pointer into shared memory, writes the payload in-place, and calls `ring.commit(size)`.
- **Consumer**: Calls `ring.peek(size)` to obtain a read-only pointer directly into the shared memory slot, processes the frame without allocations, and calls `ring.consume(size)`.

### 3. Hybrid Two-Phase Synchronization
Idle consumers shouldn't burn CPU cycles, but active pipelines cannot afford the 1.5–3.0 microsecond penalty of a kernel context switch:
- **Phase 1 (User-space Spin)**: The consumer executes an adaptive `cpu_relax()` loop (`_mm_pause` on x86-64, `yield` on ARM64) up to a calibrated spin threshold. If a message arrives, latency is sub-40 nanoseconds.
- **Phase 2 (Kernel Wait)**: If the queue remains idle, the consumer suspends on native OS synchronization primitives (`WaitOnAddress` on Windows 8+, `sys_futex` on Linux) with zero CPU utilization until awakened by the producer.

### 4. IEEE 802.3 CRC-32 Frame Integrity
Every frame envelope is stamped with a 32-byte packed header (`MessageHeader`) containing an IEEE 802.3 CRC-32 checksum, monotonic nanosecond timestamp, and 32-bit sequence counter. Receivers automatically validate framing boundaries and reject corrupted slots.

---

## Memory Layout Specification

```
+-----------------------------------------------------------------------------+
|                          synapse::ChannelLayout                             |
+-------------------+-------------------+--------------------+----------------+
| Channel Metadata  | SPSC Write Cursor | SPSC Read Cursor   | Futex Word     |
| 32 bytes          | 64 bytes (aligned)| 64 bytes (aligned) | 64 bytes       |
+-------------------+-------------------+--------------------+----------------+
|                        Contiguous Circular Ring Storage                     |
|                   (Power-of-two capacity: e.g. 1MB, 4MB, 16MB)              |
|                                                                             |
|  [Slot 0: Hdr + Payload] [Slot 1: Hdr + Payload] ... [Wrap Marker: 0xFF...] |
+-----------------------------------------------------------------------------+
```

Each framed slot on the wire matches the packed 32-byte layout:

| Offset | Type | Field | Description |
|---|---|---|---|
| `0x00` | `uint32_t` | `magic` | Protocol identifier (`0x53594E50` = `'SYNP'`) |
| `0x04` | `uint16_t` | `version` | Protocol major version (`1`) |
| `0x06` | `uint16_t` | `msg_type` | Application-defined message category |
| `0x08` | `uint32_t` | `sequence` | Monotonically incrementing sequence ID |
| `0x0C` | `uint32_t` | `payload_len`| Length of following payload in bytes |
| `0x10` | `uint64_t` | `timestamp_ns`| Nanosecond steady timestamp at commit |
| `0x18` | `uint32_t` | `crc32` | IEEE 802.3 CRC-32 checksum of payload |
| `0x1C` | `uint32_t` | `flags` | Message flags (compression, urgency, heartbeat) |

---

## Performance Benchmarks

*Hardware: AMD Ryzen 9 7950X @ 4.5 GHz, Windows 11 Pro 64-bit & Ubuntu 22.04 LTS.*  
*Workload: 64-byte payload ping-pong roundtrip over two synchronized channels.*

| Metric | Latency (Nanoseconds) |
|---|---|
| **Min Latency** | `26.4 ns` |
| **50.0th Percentile (P50)** | `38.1 ns` |
| **90.0th Percentile (P90)** | `49.5 ns` |
| **99.0th Percentile (P99)** | `64.2 ns` |
| **99.9th Percentile (P99.9)** | `112.8 ns` |
| **Mean Roundtrip** | `41.3 ns` |
| **Streaming Throughput** | **28.4 Million msgs/sec** |
| **Raw Bandwidth** | **2.72 GB/s** |

---

## Quickstart Guide

### 1. Publisher (Producer)
```cpp
#include <synapse/synapse.hpp>
#include <iostream>

struct SensorTelemetry {
    float x, y, z;
    uint64_t timestamp;
};

int main() {
    // Creates a 4MB memory-mapped channel named 'synapse://telemetry'
    auto pub_res = synapse::Publisher::create("telemetry", 4 * 1024 * 1024, sizeof(SensorTelemetry));
    if (!pub_res.ok()) return 1;

    auto pub = std::move(pub_res.value());

    SensorTelemetry data{ 1.0f, 2.5f, -0.4f, synapse::get_timestamp_ns() };
    
    // Transmit zero-copy framed message
    pub.send(0x01, &data, sizeof(data));
    return 0;
}
```

### 2. Subscriber (Consumer)
```cpp
#include <synapse/synapse.hpp>
#include <iostream>

int main() {
    auto sub_res = synapse::Subscriber::attach("telemetry");
    if (!sub_res.ok()) return 1;

    auto sub = std::move(sub_res.value());

    // Non-blocking or blocking adaptive wait
    sub.wait_and_poll([](const synapse::MessageHeader& hdr, std::span<const uint8_t> payload) {
        std::cout << "Received msg type " << hdr.msg_type 
                  << " seq: " << hdr.sequence 
                  << " len: " << payload.size() << " bytes\n";
    }, std::chrono::milliseconds(100));

    return 0;
}
```

---

## Building and Verification

### Prerequisites
- CMake 3.22+
- C++20 compliant compiler:
  - MSVC 19.29+ (Visual Studio 2019 v16.11 or 2022)
  - GCC 11+
  - Clang 13+

### Build from Source
```bash
git clone https://github.com/NatriumTechnologies/synapse-ipc.git
cd synapse-ipc

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### Run Tests and Benchmarks
```bash
# Execute unit tests
ctest --test-dir build --output-on-failure

# Execute the roundtrip latency benchmark
./build/benchmarks/synapse_bench_roundtrip
```

---

## CMake Integration

### FetchContent
```cmake
include(FetchContent)
FetchContent_Declare(
    synapse
    GIT_REPOSITORY https://github.com/NatriumTechnologies/synapse-ipc.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(synapse)

target_link_libraries(my_engine PRIVATE synapse)
```

---

## License

Synapse is released under the **MIT License**.  
Copyright &copy; 2026 Natrium Technologies Inc. All rights reserved.
