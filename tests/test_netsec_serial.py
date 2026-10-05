import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('netsec_serial', Path(__file__).resolve().parents[1] / 'tools/netsec_serial.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class CaptureTests(unittest.TestCase):
    def capture(self, payload=b'\xc0\x00' + bytes(22)):
        header = struct.pack('<IHHiIII', 0xa1b2c3d4, 2, 4, 0, 0, 36, 105)
        record = struct.pack('<IIII', 1, 1234, len(payload), 100) + payload
        return ['PCAP_BEGIN 1', 'ordinary log', 'PCAP_DATA ' + header.hex(), 'PCAP_DATA ' + record.hex(), 'PCAP_END']

    def test_valid(self):
        self.assertEqual(len(module.decode_pcap(self.capture())), 64)

    def test_missing_end(self):
        with self.assertRaises(ValueError):
            module.decode_pcap(self.capture()[:-1])

    def test_bad_record_count(self):
        lines = self.capture()
        lines[0] = 'PCAP_BEGIN 2'
        with self.assertRaises(ValueError):
            module.decode_pcap(lines)

    def test_bad_lengths(self):
        with self.assertRaises(ValueError):
            module.decode_pcap(self.capture(bytes(37)))

    def test_bad_hex(self):
        lines = self.capture()
        lines[2] = 'PCAP_DATA zz'
        with self.assertRaises(ValueError):
            module.decode_pcap(lines)

if __name__ == '__main__':
    unittest.main()
