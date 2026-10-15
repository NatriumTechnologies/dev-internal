import asyncio
import ipaddress
import socket
import time
from dataclasses import dataclass
from typing import Optional, Callable

SWEEP_PROBE_PORTS = [80, 443, 22, 445, 8080, 135, 3389, 53]

@dataclass
class HostResult:
    ip: str
    hostname: str
    status: str
    open_ports: list[int]
    rtt_ms: float

class SubnetSweeper:
    def __init__(
        self,
        cidr: str,
        timeout: float = 0.8,
        concurrency: int = 128,
        probe_ports: Optional[list[int]] = None
    ):
        self.cidr = cidr
        self.timeout = timeout
        self.concurrency = concurrency
        self.probe_ports = probe_ports or SWEEP_PROBE_PORTS

    def get_hosts(self) -> list[str]:
        if "/" in self.cidr:
            net = ipaddress.ip_network(self.cidr, strict=False)
            return [str(ip) for ip in net.hosts()]
        elif "-" in self.cidr:
            # Range format e.g. 192.168.1.1-50
            prefix, end_part = self.cidr.rsplit(".", 1)
            start_str, end_str = end_part.split("-", 1)
            return [f"{prefix}.{i}" for i in range(int(start_str), int(end_str) + 1)]
        else:
            return [self.cidr]

    async def probe_host(
        self,
        sem: asyncio.Semaphore,
        ip: str,
        progress_cb: Optional[Callable[[int, int], None]] = None,
        counter: Optional[list[int]] = None,
        total: int = 0
    ) -> Optional[HostResult]:
        async with sem:
            open_ports = []
            best_rtt = 9999.0
            is_alive = False

            t0 = time.perf_counter()
            for port in self.probe_ports:
                try:
                    conn = asyncio.open_connection(ip, port)
                    _, writer = await asyncio.wait_for(conn, timeout=self.timeout)
                    rtt = (time.perf_counter() - t0) * 1000.0
                    best_rtt = min(best_rtt, rtt)
                    open_ports.append(port)
                    is_alive = True
                    writer.close()
                    await writer.wait_closed()
                except Exception:
                    pass

            if progress_cb and counter is not None:
                counter[0] += 1
                progress_cb(counter[0], total)

            if is_alive:
                # Try reverse DNS lookup
                try:
                    hostname = socket.gethostbyaddr(ip)[0]
                except Exception:
                    hostname = ""

                return HostResult(
                    ip=ip,
                    hostname=hostname,
                    status="alive",
                    open_ports=open_ports,
                    rtt_ms=round(best_rtt, 2)
                )

            return None

    async def run(
        self,
        progress_cb: Optional[Callable[[int, int], None]] = None
    ) -> list[HostResult]:
        hosts = self.get_hosts()
        sem = asyncio.Semaphore(self.concurrency)
        counter = [0]
        total = len(hosts)

        tasks = [
            self.probe_host(sem, ip, progress_cb, counter, total)
            for ip in hosts
        ]

        raw = await asyncio.gather(*tasks)
        alive = [h for h in raw if h is not None]
        alive.sort(key=lambda x: ipaddress.ip_address(x.ip))
        return alive
