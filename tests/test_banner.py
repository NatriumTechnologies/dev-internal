import unittest
from netprobe.banner import BannerGrabber

class TestBannerGrabber(unittest.TestCase):
    def test_clean_banner_ascii(self):
        raw = b"SSH-2.0-OpenSSH_9.0p1 Debian\r\n"
        cleaned = BannerGrabber.clean_banner(raw)
        self.assertEqual(cleaned, "SSH-2.0-OpenSSH_9.0p1 Debian")

    def test_clean_banner_non_printable(self):
        raw = b"\x00\x05Apache\x01\x02/2.4.52\r\n"
        cleaned = BannerGrabber.clean_banner(raw)
        self.assertEqual(cleaned, "Apache /2.4.52")

    def test_clean_banner_truncation(self):
        raw = b"A" * 200
        cleaned = BannerGrabber.clean_banner(raw)
        self.assertEqual(len(cleaned), 120)

if __name__ == "__main__":
    unittest.main()
