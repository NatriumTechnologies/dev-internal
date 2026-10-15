import unittest
from netprobe.sweep import SubnetSweeper

class TestSubnetSweeper(unittest.TestCase):
    def test_get_hosts_cidr(self):
        sweeper = SubnetSweeper("192.168.1.0/30")
        hosts = sweeper.get_hosts()
        self.assertEqual(hosts, ["192.168.1.1", "192.168.1.2"])

    def test_get_hosts_range(self):
        sweeper = SubnetSweeper("10.0.0.5-8")
        hosts = sweeper.get_hosts()
        self.assertEqual(hosts, ["10.0.0.5", "10.0.0.6", "10.0.0.7", "10.0.0.8"])

    def test_get_hosts_single(self):
        sweeper = SubnetSweeper("127.0.0.1")
        hosts = sweeper.get_hosts()
        self.assertEqual(hosts, ["127.0.0.1"])

if __name__ == "__main__":
    unittest.main()
