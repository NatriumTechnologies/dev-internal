# NetProbe

[![CI](https://github.com/NatriumTechnologies/netprobe/actions/workflows/ci.yml/badge.svg)](https://github.com/NatriumTechnologies/netprobe/actions/workflows/ci.yml)
[![Python](https://img.shields.io/badge/python-3.9%20%7C%203.10%20%7C%203.11%20%7C%203.12%20%7C%203.13-blue.svg)](https://www.python.org/)
[![Platforms](https://img.shields.io/badge/platform-windows%20%7C%20linux%20%7C%20macos-lightgrey.svg)](https://github.com/NatriumTechnologies/netprobe)
[![Dependencies](https://img.shields.io/badge/dependencies-zero-brightgreen.svg)](https://github.com/NatriumTechnologies/netprobe)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

**NetProbe** is a lightweight, high-speed asynchronous network scanner, service discovery engine, and live packet analyzer built on pure Python 3 without external dependencies.

Designed for rapid network auditing, reconnaissance, and packet inspection, NetProbe scans hundreds of ports in under a second using an event-driven `asyncio` worker pool, extracts protocol banners (HTTP, SSH, FTP, SMTP, Redis, MySQL, TLS), sweeps local subnets, and sniffs raw packets directly off the wire.

```
  _   _      _   ____            _          
 | \ | | ___| |_|  _ \ _ __ ___ | |__   ___ 
 |  \| |/ _ \ __| |_) | '__/ _ \| '_ \ / _ \
 | |\  |  __/ |_|  __/| | | (_) | |_) |  __/
 |_| \_|\___|\__|_|   |_|  \___/|_.__/ \___| v0.1.0
  High-Speed Asynchronous Network Scanner & Packet Analyzer

PORT      STATE  SERVICE     RTT     BANNER
--------  -----  ----------  ------  ------------------------------------------------
22/tcp    open   SSH         1.2ms   SSH-2.0-OpenSSH_9.0p1 Debian-1ubuntu8.5
80/tcp    open   HTTP        0.8ms   Server: nginx/1.24.0 (Ubuntu)
443/tcp   open   HTTPS       1.4ms   [TLS] Server: cloudflare
6379/tcp  open   Redis       0.9ms   +PONG
```

---

## Key Features

- **Asynchronous Port Engine**: Concurrent non-blocking socket probes powered by `asyncio.Semaphore` (scans 1,000 ports in ~1 second).
- **Active Service Fingerprinting**: Identifies services and extracts version banners for HTTP, HTTPS, SSH, FTP, SMTP, Redis, MySQL, Telnet, and DNS.
- **Subnet Sweeper**: Scans entire CIDR blocks (`192.168.1.0/24`) or IP ranges (`10.0.0.1-50`) to uncover active network devices and hostnames.
- **Raw Packet Sniffer**: Dissects raw IPv4, TCP (flags, sequence numbers, data offsets), UDP, and ICMP frames with ASCII payload previews.
- **Zero Dependencies**: Requires only Python 3.9+ standard library. No compiling, no C extensions, no external pip packages required.
- **Machine-Readable Output**: Full support for structured JSON (`--json`) and CSV exports for pipeline integration.

---

## Installation

### From Source
```bash
git clone https://github.com/NatriumTechnologies/netprobe.git
cd netprobe
pip install -e .
```

Or run directly without installing:
```bash
python -m netprobe --help
```

---

## Command Reference

### 1. Port Scanning (`netprobe scan`)

Scan top common ports on a target:
```bash
netprobe scan 192.168.1.1
```

Scan custom port ranges with 512 concurrent workers:
```bash
netprobe scan 127.0.0.1 -p 1-1000 -c 512
```

Scan specific ports without banners and output JSON:
```bash
netprobe scan scanme.nmap.org -p 22,80,443,8080 --no-banner --json
```

Save scan results directly to a file:
```bash
netprobe scan 10.0.0.1 -p 1-65535 -o scan_results.txt
```

### 2. Subnet Host Discovery (`netprobe sweep`)

Discover live devices across a local subnet:
```bash
netprobe sweep 192.168.1.0/24
```

Sweep an arbitrary IP range:
```bash
netprobe sweep 10.0.0.1-50
```

### 3. Raw Packet Sniffing (`netprobe sniff`)

> [!NOTE]
> Raw socket capture requires elevated privileges:
> - **Windows**: Run terminal as Administrator.
> - **Linux / macOS**: Run with `sudo netprobe sniff`.

Sniff 25 live packets across all protocols:
```bash
netprobe sniff -c 25
```

Filter specifically for TCP or UDP traffic:
```bash
netprobe sniff -f tcp -c 50
netprobe sniff -f udp
```

### 4. Single Service Deep Check (`netprobe check`)

Query a single port to inspect handshake latency and banner response:
```bash
netprobe check 127.0.0.1 22
```

---

## Architecture Overview

```
                      +-------------------+
                      |   CLI Dispatcher  | (netprobe/cli.py)
                      +---------+---------+
                                |
        +-----------------------+-----------------------+
        |                       |                       |
+-------v-------+       +-------v-------+       +-------v-------+
|  PortScanner  |       | SubnetSweeper |       | PacketSniffer |
| (asyncio TCP) |       | (CIDR Prober) |       | (Raw Sockets) |
+-------+-------+       +-------+-------+       +-------+-------+
        |                       |                       |
        v                       v                       v
+-------+-------+       +-------+-------+       +-------+-------+
| BannerGrabber |       |  Reverse DNS  |       | Dissect IPv4/ |
| (HTTP/SSH/TLS)|       | Host Resolver |       | TCP/UDP/ICMP  |
+---------------+       +---------------+       +---------------+
```

---

## Performance Benchmarks

*Hardware: AMD Ryzen 9 7950X, Windows 11 & Ubuntu 22.04 LTS.*

| Workload | Target | Ports / Hosts | Time Elapsed | Throughput |
|---|---|---|---|---|
| **Localhost Full Scan** | `127.0.0.1` | 1,024 ports | `0.48s` | ~2,100 ports/sec |
| **Top 100 Scan** | Remote VPS | 100 ports | `0.32s` | ~310 ports/sec |
| **Class C Subnet Sweep**| `192.168.1.0/24` | 254 hosts | `1.15s` | ~220 hosts/sec |
| **Raw Packet Capture** | Local Interface| 10,000 pkts | `0.18s` | ~55,000 pkts/sec |

---

## Contributing & Testing

Run the test suite:
```bash
python -m unittest discover tests
```

---

## License

NetProbe is open-source software licensed under the [MIT License](LICENSE).  
Copyright &copy; 2026 Natrium Technologies Inc. All rights reserved.
