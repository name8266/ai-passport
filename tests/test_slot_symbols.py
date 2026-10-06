#!/usr/bin/env python3
"""Static integrity checks for the six shared RGB565 reel-strip sprites."""
import re
import struct
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NAMES = ('cherry', 'lemon', 'bar', 'bell', 'plum', 'seven')


class SlotSymbols(unittest.TestCase):
    def test_original_source_and_tile_dimensions(self):
        for name, size in [('slot-classic-atlas', (1536, 1024)),
                           *[(f'slot-{n}', (48, 64)) for n in NAMES],
                           ('slot-symbols-native', (288, 64)),
                           *[(f'slot-small-{n}', (32, 36)) for n in NAMES],
                           ('slot-symbols-small-native', (192, 36))]:
            png = (ROOT / f'assets/images/{name}.png').read_bytes()
            self.assertEqual(png[:8], b'\x89PNG\r\n\x1a\n')
            self.assertEqual(struct.unpack('>II', png[16:24]), size)

    def test_complete_unique_flash_sources(self):
        source = (ROOT / 'assets/images/slot_symbols.c').read_text()
        arrays = re.findall(r'static const uint8_t slot_(\w+)_pixels\[6144\].*?= \{(.*?)\};', source, re.S)
        self.assertEqual(tuple(name for name, _ in arrays), NAMES)
        unique = set()
        background = (247 >> 3) << 11 | (241 >> 2) << 5 | (227 >> 3)
        for _, data in arrays:
            raw = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-f]{2})', data))
            self.assertEqual(len(raw), 48 * 64 * 2)
            colors = struct.unpack('<3072H', raw)
            self.assertGreater(len(set(colors)), 100)
            self.assertGreater(sum(v != background for v in colors), 300)
            for corner in (0, 47, 48 * 63, 3071):
                self.assertEqual(colors[corner], background)
            unique.add(raw)
        self.assertEqual(len(unique), 6)
        self.assertEqual(source.count('.cf=LV_COLOR_FORMAT_RGB565'), 12)
        self.assertEqual(source.count('.w=48, .h=64, .stride=96'), 6)
        descriptors = re.findall(r'\.data=slot_(\w+)_pixels', source)
        self.assertEqual(tuple(descriptors), NAMES + tuple('small_' + n for n in NAMES))
        small = re.findall(r'static const uint8_t slot_small_(\w+)_pixels\[2304\].*?= \{(.*?)\};', source, re.S)
        self.assertEqual(tuple(n for n, _ in small), NAMES)
        for _, data in small:
            self.assertEqual(len(re.findall(r'0x([0-9a-f]{2})', data)), 2304)
        self.assertEqual(source.count('.w=32, .h=36, .stride=64'), 6)


if __name__ == '__main__':
    unittest.main()
