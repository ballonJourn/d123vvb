"""The soft AP channel rules (wlan_ap_channel.h), compiled and run on the PC: `wifi ap SSID PASSWORD CHANNEL`
accepts what the AIC8800 driver accepts (2.4 GHz 1..13, 5 GHz 36 40 44 48 149 153 157 161 165) and nothing else,
the old default is still channel 6 on 2.4 GHz, and a channel from 36 up means 5 GHz."""
import os
import subprocess
import tempfile
import unittest

HOST = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "host")
VALID_24 = set(range(1, 14))
VALID_5 = {36, 40, 44, 48, 149, 153, 157, 161, 165}


class WlanChannelTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.exe = os.path.join(tempfile.mkdtemp(prefix="wlanch_"), "t")
        subprocess.check_call(["cc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-o", cls.exe,
                               os.path.join(HOST, "test_wlan_channel.c")])
        out = subprocess.check_output([cls.exe], text=True).split("\n")
        cls.default = [int(x) for x in out[0].split()[1:]]
        cls.table = {int(a): (int(b), int(c)) for a, b, c in (l.split() for l in out[1:] if l.strip())}

    def test_exactly_the_driver_channels_are_valid(self):
        valid = {c for c, (ok, _) in self.table.items() if ok}
        self.assertEqual(valid, VALID_24 | VALID_5)

    def test_the_old_default_is_unchanged(self):
        self.assertEqual(self.default, [6, 1, 0])                     # channel 6, valid, 2.4 GHz

    def test_band_follows_the_channel_number(self):
        for c in VALID_24:
            self.assertEqual(self.table[c][1], 0, c)
        for c in VALID_5:
            self.assertEqual(self.table[c][1], 1, c)

    def test_things_that_must_be_refused(self):
        for c in (-1, 0, 14, 15, 35, 37, 52, 64, 100, 140, 148, 150, 166, 200):    # 52..144 are DFS channels the driver does not list
            self.assertEqual(self.table[c][0], 0, c)


if __name__ == "__main__":
    unittest.main()
