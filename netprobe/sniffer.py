import socket
import struct
import sys
import os
from dataclasses import dataclass
from typing import Optional, Generator

@dataclass
class DissectedPacket:
    src_ip: str
    dst_ip: str
    protocol: str
    ttl: int
    length: int
    src_port: Optional[int]
    dst_port: Optional[int]
    flags_str: str
    payload_preview: str

class PacketSniffer:
    PROTO_MAP = {
        1: "ICMP",
        6: "TCP",
        17: "UDP",
    }

    def __init__(self, interface_ip: Optional[str] = None, proto_filter: Optional[str] = None):
        self.interface_ip = interface_ip or self._detect_default_ip()
        self.proto_filter = proto_filter.upper() if proto_filter else None
        self.sock: Optional[socket.socket] = None

    @staticmethod
    def _detect_default_ip() -> str:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("8.8.8.8", 80))
            ip = s.getsockname()[0]
            s.close()
            return ip
        except Exception:
            return "127.0.0.1"

    def open(self) -> None:
        try:
            if os.name == "nt":
                self.sock = socket.socket(socket.AF_INET, socket.SOCK_RAW, socket.IPPROTO_IP)
                self.sock.bind((self.interface_ip, 0))
                self.sock.setsockopt(socket.IPPROTO_IP, socket.IP_HDRINCL, 1)
                # Windows promiscuous mode ioctl
                self.sock.ioctl(socket.SIO_RCVALL, socket.RCVALL_ON)
            else:
                self.sock = socket.socket(socket.AF_INET, socket.SOCK_RAW, socket.IPPROTO_TCP)
                self.sock.bind((self.interface_ip, 0))
        except PermissionError:
            raise PermissionError(
                "Raw packet sniffing requires administrator/root privileges.\n"
                "  Windows: Run terminal as Administrator\n"
                "  Linux:   Run with 'sudo netprobe sniff'"
            )

    def close(self) -> None:
        if self.sock:
            if os.name == "nt":
                try:
                    self.sock.ioctl(socket.SIO_RCVALL, socket.RCVALL_OFF)
                except Exception:
                    pass
            self.sock.close()
            self.sock = None

    def sniff(self, count: int = 20) -> Generator[DissectedPacket, None, None]:
        if not self.sock:
            self.open()

        captured = 0
        while captured < count:
            raw_data, _ = self.sock.recvfrom(65535)
            packet = self.dissect(raw_data)
            if not packet:
                continue

            if self.proto_filter and packet.protocol != self.proto_filter:
                continue

            yield packet
            captured += 1

    @classmethod
    def dissect(cls, raw: bytes) -> Optional[DissectedPacket]:
        if len(raw) < 20:
            return None

        # Dissect IPv4 Header (RFC 791)
        ip_header = struct.unpack("!BBHHHBBH4s4s", raw[:20])
        version_ihl = ip_header[0]
        ihl = (version_ihl & 0x0F) * 4
        ttl = ip_header[5]
        proto_num = ip_header[6]
        src_ip = socket.inet_ntoa(ip_header[8])
        dst_ip = socket.inet_ntoa(ip_header[9])
        total_len = ip_header[2]

        protocol_name = cls.PROTO_MAP.get(proto_num, f"IP({proto_num})")
        payload = raw[ihl:]
        src_port = None
        dst_port = None
        flags_str = ""

        if proto_num == 6 and len(payload) >= 20:  # TCP
            tcp_header = struct.unpack("!HHLLBBHHH", payload[:20])
            src_port = tcp_header[0]
            dst_port = tcp_header[1]
            flags_byte = tcp_header[5]

            # Parse TCP Flags
            flags = []
            if flags_byte & 0x02: flags.append("SYN")
            if flags_byte & 0x10: flags.append("ACK")
            if flags_byte & 0x01: flags.append("FIN")
            if flags_byte & 0x04: flags.append("RST")
            if flags_byte & 0x08: flags.append("PSH")
            if flags_byte & 0x20: flags.append("URG")
            flags_str = ",".join(flags) if flags else "."

            data_offset = (tcp_header[4] >> 4) * 4
            tcp_data = payload[data_offset:]
            preview = cls._format_preview(tcp_data)

        elif proto_num == 17 and len(payload) >= 8:  # UDP
            udp_header = struct.unpack("!HHHH", payload[:8])
            src_port = udp_header[0]
            dst_port = udp_header[1]
            flags_str = "LEN=" + str(udp_header[2])
            preview = cls._format_preview(payload[8:])

        elif proto_num == 1 and len(payload) >= 4:  # ICMP
            icmp_type, icmp_code = struct.unpack("!BB", payload[:2])
            flags_str = f"TYPE={icmp_type},CODE={icmp_code}"
            preview = cls._format_preview(payload[4:])

        else:
            preview = cls._format_preview(payload)

        return DissectedPacket(
            src_ip=src_ip,
            dst_ip=dst_ip,
            protocol=protocol_name,
            ttl=ttl,
            length=total_len,
            src_port=src_port,
            dst_port=dst_port,
            flags_str=flags_str,
            payload_preview=preview
        )

    @staticmethod
    def _format_preview(data: bytes, max_len: int = 32) -> str:
        if not data:
            return ""
        printable = "".join(chr(b) if 32 <= b <= 126 else "." for b in data[:max_len])
        return printable
