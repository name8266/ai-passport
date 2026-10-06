#!/usr/bin/env python3
"""Generate the two licensed CJK subsets with lv_font_conv 1.5.3."""
import argparse
import re
import subprocess
from pathlib import Path
root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--converter', default='lv_font_conv')
args = parser.parse_args()
text = (root / 'main/slot_app.c').read_text()
chars = ''.join(sorted(set(re.findall(r'[\u3000-\u303f\u4e00-\u9fff\uff00-\uffef]', text))))
(root / 'assets/fonts/arcade_symbols.txt').write_text(chars + '\n')
(root / 'main/arcade_font_inventory.h').write_text(
    '#pragma once\n#include <stdint.h>\nstatic const uint32_t ARCADE_CHINESE_CODEPOINTS[] = {\n    ' +
    ','.join(f'0x{ord(c):X}' for c in chars) + '\n};\n')
for size in (14, 20):
    subprocess.run([args.converter, '--font', str(root / 'assets/fonts/NotoSansCJKsc-Arcade.otf'),
                    '--range', '0x20-0x7E', '--symbols', chars, '--size', str(size),
                    '--bpp', '4', '--format', 'lvgl', '--no-compress', '--no-kerning',
                    '--lv-font-name', f'arcade_font_{size}', '--lv-include', 'lvgl.h',
                    '--output', str(root / f'assets/fonts/arcade_font_{size}.c')], check=True)
print(f'Generated arcade fonts: {len(chars)} Chinese/punctuation glyphs + ASCII, 14px and 20px')
