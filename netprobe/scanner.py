import asyncio
import time
import socket
from dataclasses import dataclass
from typing import Callable, Optional
from netprobe.banner import BannerGrabber

TOP_100_PORTS = [
    20, 21, 22, 23, 25, 53, 67, 68, 69, 80, 110, 111, 119, 123, 135, 137, 138, 139,
    143, 161, 162, 179, 389, 443, 445, 465, 514, 515, 587, 631, 636, 873, 990, 993,
    995, 1080, 1194, 1433, 1521, 1723, 1812, 1813, 2049, 2082, 2083, 2086, 2087, 2181,
    2222, 2375, 2376, 3000, 3128, 3306, 3389, 3690, 4000, 4444, 5000, 5222, 5432, 5672,
    5900, 5984, 6379, 6667, 7000, 7001, 8000, 8008, 8080, 8081, 8443, 8888, 9000, 9090,
    9092, 9200, 9418, 9999, 11211, 27017, 27018, 28017, 50000, 50070
]

@dataclass
class ScanResult:
    port: int
    state: str
    rtt_ms: float
    service: str
    banner: str

class PortScanner:
    def __init__(
        self,
        target: str,
        ports: list[int],
        timeout: float = 1.2,
        concurrency: int = 256,
        grab_banners: bool = True
    ):
        self.target = target
        self.ports = sorted(list(set(ports)))
        self.timeout = timeout
        self.concurrency = concurrency
        self.grab_banners = grab_banners
        self.resolved_ip: Optional[str] = None

    def resolve(self) -> str:
        if not self.resolved_ip:
            self.resolved_ip = socket.gethostbyname(self.target)
        return self.resolved_ip

    async def probe_port(
        self,
        sem: asyncio.Semaphore,
        port: int,
        progress_cb: Optional[Callable[[int, int], None]] = None,
        done_counter: Optional[list[int]] = None
    ) -> Optional[ScanResult]:
        async with sem:
            t0 = time.perf_counter()
            try:
                conn = asyncio.open_connection(self.resolved_ip, port)
                reader, writer = await asyncio.wait_for(conn, timeout=self.timeout)
                rtt = (time.perf_counter() - t0) * 1000.0
                writer.close()
                await writer.wait_closed()

                service = "Unknown"
                banner = ""
                if self.grab_banners:
                    service, banner = await BannerGrabber.grab(self.resolved_ip, port, timeout=self.timeout)

                res = ScanResult(
                    port=port,
                    state="open",
                    rtt_ms=round(rtt, 2),
                    service=service,
                    banner=banner
                )
            except (asyncio.TimeoutError, ConnectionRefusedError, OSError):
                res = None
            finally:
                if progress_cb and done_counter is not None:
                    done_counter[0] += 1
                    progress_cb(done_counter[0], len(self.ports))

            return res

    async def run(
        self,
        progress_cb: Optional[Callable[[int, int], None]] = None
    ) -> list[ScanResult]:
        self.resolve()
        sem = asyncio.Semaphore(self.concurrency)
        done_counter = [0]

        tasks = [
            self.probe_port(sem, p, progress_cb, done_counter)
            for p in self.ports
        ]

        raw_results = await asyncio.gather(*tasks)
        open_ports = [r for r in raw_results if r is not None]
        open_ports.sort(key=lambda x: x.port)
        return open_ports

    @staticmethod
    def parse_ports(spec: str) -> list[int]:
        ports: set[int] = set()
        parts = spec.split(",")
        for part in parts:
            part = part.strip()
            if not part:
                continue
            if "-" in part:
                start_str, end_str = part.split("-", 1)
                start, end = int(start_str), int(end_str)
                for p in range(max(1, start), min(65535, end) + 1):
                    ports.add(p)
            else:
                p = int(part)
                if 1 <= p <= 65535:
                    ports.add(p)
        return sorted(list(ports))
