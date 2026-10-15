import unittest
from netprobe.scanner import PortScanner

class TestPortScanner(unittest.TestCase):
    def test_parse_ports_single_and_comma(self):
        ports = PortScanner.parse_ports("80,443,8080")
        self.assertEqual(ports, [80, 443, 8080])

    def test_parse_ports_range(self):
        ports = PortScanner.parse_ports("20-25")
        self.assertEqual(ports, [20, 21, 22, 23, 24, 25])

    def test_parse_ports_combined(self):
        ports = PortScanner.parse_ports("22,80-82,443")
        self.assertEqual(ports, [22, 80, 81, 82, 443])

    def test_parse_ports_dedup_and_sort(self):
        ports = PortScanner.parse_ports("80,80,22,22,443")
        self.assertEqual(ports, [22, 80, 443])

if __name__ == "__main__":
    unittest.main()
