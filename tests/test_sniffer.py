import unittest
import struct
import socket
from netprobe.sniffer import PacketSniffer

class TestPacketSniffer(unittest.TestCase):
    def test_dissect_ipv4_tcp_packet(self):
        # Build synthetic 40-byte IPv4 + TCP packet
        src_ip = socket.inet_aton("192.168.1.100")
        dst_ip = socket.inet_aton("10.0.0.1")

        # IPv4 header: 20 bytes, Version=4, IHL=5, Protocol=6 (TCP)
        ip_hdr = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 40, 1234, 0x4000, 64, 6, 0, src_ip, dst_ip)

        # TCP header: 20 bytes, src=12345, dst=80, SYN+ACK (0x12), data_offset=5 (20 bytes)
        tcp_hdr = struct.pack("!HHLLBBHHH", 12345, 80, 1000, 2000, 0x50, 0x12, 65535, 0, 0)

        raw = ip_hdr + tcp_hdr
        packet = PacketSniffer.dissect(raw)

        self.assertIsNotNone(packet)
        self.assertEqual(packet.src_ip, "192.168.1.100")
        self.assertEqual(packet.dst_ip, "10.0.0.1")
        self.assertEqual(packet.protocol, "TCP")
        self.assertEqual(packet.src_port, 12345)
        self.assertEqual(packet.dst_port, 80)
        self.assertIn("SYN", packet.flags_str)
        self.assertIn("ACK", packet.flags_str)

    def test_dissect_ipv4_udp_packet(self):
        src_ip = socket.inet_aton("172.16.0.5")
        dst_ip = socket.inet_aton("8.8.8.8")

        # IPv4 header: Protocol=17 (UDP)
        ip_hdr = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 28, 5678, 0, 64, 17, 0, src_ip, dst_ip)
        # UDP header: src=53, dst=5353, len=8
        udp_hdr = struct.pack("!HHHH", 53, 5353, 8, 0)

        raw = ip_hdr + udp_hdr
        packet = PacketSniffer.dissect(raw)

        self.assertIsNotNone(packet)
        self.assertEqual(packet.protocol, "UDP")
        self.assertEqual(packet.src_port, 53)
        self.assertEqual(packet.dst_port, 5353)

if __name__ == "__main__":
    unittest.main()
