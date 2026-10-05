import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def covered(text):
    arrays = {
        name: [int(v, 0) for v in re.findall(r"0x[0-9a-fA-F]+|\b\d+\b", body)]
        for name, body in re.findall(r"static const uint16_t (unicode_list_\d+)\[\] = \{(.*?)\};", text, re.S)
    }
    offsets = {
        name: [int(v, 0) for v in re.findall(r"0x[0-9a-fA-F]+|\b\d+\b", body)]
        for name, body in re.findall(r"static const uint8_t (glyph_id_ofs_list_\d+)\[\] = \{(.*?)\};", text, re.S)
    }
    cmaps = re.search(r"static const lv_font_fmt_txt_cmap_t cmaps\[\] =\s*\{(.*?)\n\};", text, re.S)[1]
    actual = set()
    for block in re.findall(r"\{(.*?)\}", cmaps, re.S):
        start = int(re.search(r"\.range_start = (\d+)", block)[1])
        length = int(re.search(r"\.range_length = (\d+)", block)[1])
        name = re.search(r"\.unicode_list = (\w+)", block)[1]
        ofs = re.search(r"\.glyph_id_ofs_list = (\w+)", block)[1]
        if name != "NULL":
            actual.update(start + v for v in arrays[name])
        elif ofs != "NULL":
            actual.update(start + i for i, v in enumerate(offsets[ofs]) if i == 0 or v != 0)
        else:
            actual.update(range(start, start + length))
    return actual


class StockFontTests(unittest.TestCase):
    def test_coverage(self):
        for font, manifest in [("stock_font_16.c", "stock_font_manifest.json"), ("stock_font_12.c", "stock_ui_manifest.json")]:
            points = set(json.loads((ROOT / "assets/fonts" / manifest).read_text())["codepoints"])
            self.assertEqual(covered((ROOT / "assets/fonts" / font).read_text()), points)
            for file in ["stock_app.c", "stock_network.c"]:
                for c in (ROOT / "main" / file).read_text():
                    if c.isprintable() and ord(c) >= 128:
                        self.assertIn(ord(c), points, f"{font} missing {c!r}")
        points = covered((ROOT / "assets/fonts/stock_font_16.c").read_text())
        for a in range(0x81, 0xff):
            for b in range(0x40, 0xff):
                try:
                    c = bytes([a, b]).decode("gbk")
                except UnicodeDecodeError:
                    continue
                if 0x4e00 <= ord(c) <= 0x9fff:
                    self.assertIn(ord(c), points)

    def test_bitmap_bounds(self):
        for font in ["stock_font_16.c", "stock_font_12.c"]:
            text = (ROOT / "assets/fonts" / font).read_text()
            raw = re.search(r"glyph_bitmap\[\].*?=\s*\{(.*?)\};", text, re.S)[1]
            raw = re.sub(r"/\*.*?\*/", "", raw, flags=re.S)
            size = len(re.findall(r"0x[0-9a-fA-F]+", raw))
            for index, w, h in re.findall(r"\.bitmap_index = (\d+), \.adv_w = \d+, \.box_w = (\d+), \.box_h = (\d+)", text):
                self.assertLessEqual(int(index) + (int(w) * int(h) + 3) // 4, size)


if __name__ == "__main__":
    unittest.main()
