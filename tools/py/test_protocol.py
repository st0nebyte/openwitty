import sys, unittest, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[0]))
from lewitt_connect2 import protocol as P


class ProtocolTests(unittest.TestCase):
    def test_message_table(self):
        self.assertEqual(P.MESSAGES[P.MSG_SETTINGS].size, 0x80)
        self.assertTrue(P.MESSAGES[P.MSG_SETTINGS].writable)
        self.assertFalse(P.MESSAGES[P.MSG_STATUS].writable)
        self.assertFalse(P.MESSAGES[P.MSG_TRIGGERS].readable)

    def test_setup_fields(self):
        self.assertEqual(P.wvalue_for(P.MSG_VERSION), 0x0103)
        self.assertEqual(P.windex_for(0), 0x3300)
        self.assertEqual(P.windex_for(2), 0x3302)

    def test_q88(self):
        self.assertEqual(P.q88_to_db(0x5500), 85.0)
        self.assertEqual(P.db_to_q88(-60.0), -0x3C00)

    def test_settings_decode(self):
        b = bytearray(0x80)
        b[0:2] = (25 * 256).to_bytes(2, "little")
        b[5] = 1
        b[6] = 2
        s = P.settings_decode(bytes(b))
        self.assertEqual(s["input1"]["gain_db"], 25.0)
        self.assertTrue(s["input1"]["phantom_power"])
        self.assertEqual(s["input1"]["preamp_type"], 2)
        with self.assertRaises(ValueError):
            P.settings_decode(b"\0" * 4)


if __name__ == "__main__":
    unittest.main()
