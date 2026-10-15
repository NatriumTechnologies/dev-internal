import asyncio
import socket
import ssl
import re

COMMON_SERVICES = {
    21: "FTP",
    22: "SSH",
    23: "Telnet",
    25: "SMTP",
    53: "DNS",
    80: "HTTP",
    110: "POP3",
    143: "IMAP",
    443: "HTTPS",
    445: "SMB",
    993: "IMAPS",
    995: "POP3S",
    1433: "MSSQL",
    1521: "Oracle",
    3306: "MySQL",
    3389: "RDP",
    5432: "PostgreSQL",
    6379: "Redis",
    8000: "HTTP-Alt",
    8080: "HTTP-Proxy",
    8443: "HTTPS-Alt",
    9200: "Elasticsearch",
    27017: "MongoDB",
}

class BannerGrabber:
    @staticmethod
    def clean_banner(raw: bytes) -> str:
        text = raw.decode("utf-8", errors="replace").strip()
        # Remove non-printable control characters
        cleaned = re.sub(r"[\x00-\x1f\x7f-\x9f]", " ", text)
        return " ".join(cleaned.split())[:120]

    @classmethod
    async def grab(cls, host: str, port: int, timeout: float = 2.0) -> tuple[str, str]:
        service_name = COMMON_SERVICES.get(port, "Unknown")
        banner_text = ""

        try:
            if port in (443, 8443):
                # TLS / HTTPS probe
                banner_text = await cls._grab_tls(host, port, timeout)
            elif port in (80, 8080, 8000, 8888, 3000, 5000):
                # Plaintext HTTP probe
                banner_text = await cls._grab_http(host, port, timeout)
            elif port == 6379:
                # Redis probe
                banner_text = await cls._grab_redis(host, port, timeout)
            else:
                # Generic banner greeting or banner read
                banner_text = await cls._grab_generic(host, port, timeout)

        except Exception:
            banner_text = ""

        return service_name, banner_text

    @classmethod
    async def _grab_http(cls, host: str, port: int, timeout: float) -> str:
        try:
            reader, writer = await asyncio.wait_for(
                asyncio.open_connection(host, port),
                timeout=timeout
            )
            probe = f"HEAD / HTTP/1.1\r\nHost: {host}\r\nUser-Agent: NetProbe/0.1\r\nConnection: close\r\n\r\n"
            writer.write(probe.encode())
            await writer.drain()

            raw = await asyncio.wait_for(reader.read(1024), timeout=timeout)
            writer.close()
            await writer.wait_closed()

            text = raw.decode("utf-8", errors="replace")
            # Look for Server header
            for line in text.splitlines():
                if line.lower().startswith("server:"):
                    return line.strip()
                if line.lower().startswith("http/"):
                    status_line = line.strip()
            return status_line if 'status_line' in locals() else cls.clean_banner(raw)
        except Exception:
            return ""

    @classmethod
    async def _grab_tls(cls, host: str, port: int, timeout: float) -> str:
        try:
            ctx = ssl.create_default_context()
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE

            reader, writer = await asyncio.wait_for(
                asyncio.open_connection(host, port, ssl=ctx),
                timeout=timeout
            )
            probe = f"HEAD / HTTP/1.1\r\nHost: {host}\r\nUser-Agent: NetProbe/0.1\r\nConnection: close\r\n\r\n"
            writer.write(probe.encode())
            await writer.drain()

            raw = await asyncio.wait_for(reader.read(1024), timeout=timeout)
            writer.close()
            await writer.wait_closed()

            text = raw.decode("utf-8", errors="replace")
            for line in text.splitlines():
                if line.lower().startswith("server:"):
                    return f"[TLS] {line.strip()}"
            return "[TLS] Active HTTPS endpoint"
        except Exception:
            return "[TLS] Handshake active"

    @classmethod
    async def _grab_redis(cls, host: str, port: int, timeout: float) -> str:
        try:
            reader, writer = await asyncio.wait_for(
                asyncio.open_connection(host, port),
                timeout=timeout
            )
            writer.write(b"PING\r\n")
            await writer.drain()
            raw = await asyncio.wait_for(reader.read(256), timeout=timeout)
            writer.close()
            await writer.wait_closed()
            return cls.clean_banner(raw)
        except Exception:
            return ""

    @classmethod
    async def _grab_generic(cls, host: str, port: int, timeout: float) -> str:
        try:
            reader, writer = await asyncio.wait_for(
                asyncio.open_connection(host, port),
                timeout=timeout
            )
            # Give service a brief moment to send initial banner
            try:
                raw = await asyncio.wait_for(reader.read(512), timeout=1.0)
            except asyncio.TimeoutError:
                # If silent, send newline probe
                writer.write(b"\r\n")
                await writer.drain()
                raw = await asyncio.wait_for(reader.read(512), timeout=1.0)

            writer.close()
            await writer.wait_closed()
            return cls.clean_banner(raw)
        except Exception:
            return ""
