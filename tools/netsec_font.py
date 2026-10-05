#!/usr/bin/env python3
"""Regenerate the application font from licensed Noto Sans CJK SC, then audit it."""
import argparse
import pathlib
import re
import subprocess
import hashlib
import json

ROOT = pathlib.Path(__file__).resolve().parents[1]
RANGES = [(0x20, 0x7E), (0x3000, 0x303F), (0x4E00, 0x9FEF), (0xFF01, 0xFF60)]

def inventory(source):
    # lv_font_conv writes comments naming each encoded glyph.
    return {int(v, 16) for v in re.findall(r'/\* U\+([0-9A-Fa-f]+)', source)}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--font', type=pathlib.Path)
    parser.add_argument('--converter', default='lv_font_conv')
    parser.add_argument('--audit', action='store_true')
    args = parser.parse_args()
    output = ROOT / 'assets/fonts/netsec_font_14.c'
    if not args.audit:
        if not args.font:
            parser.error('--font is required unless --audit is used')
        command = [args.converter, '--font', str(args.font), '--range',
                   ','.join(f'0x{lo:x}-0x{hi:x}' for lo, hi in RANGES),
                   '--size', '14', '--bpp', '2', '--format', 'lvgl', '--no-compress',
                   '--lv-font-name', 'netsec_font_14', '--lv-include', 'lvgl.h',
                   '--output', str(output)]
        subprocess.run(command, check=True)
        text = output.read_text()
        text = re.sub(r' \* Opts:.*', ' * Generator: lv_font_conv 1.5.3; Noto Sans CJK SC (SIL OFL 1.1).', text, count=1)
        output.write_text(text.rstrip() + '\n')
    source = output.read_text()
    encoded = inventory(source)
    required = {cp for lo, hi in RANGES for cp in range(lo, hi + 1)}
    # CJK punctuation contains unassigned characters, which become a supported
    # replacement rather than a false claim that the source font has a glyph.
    missing = required - encoded
    han_missing = {cp for cp in missing if 0x4E00 <= cp <= 0x9FEF}
    if han_missing:
        raise SystemExit(f'Missing Han glyphs: {sorted(han_missing)}')
    fixed = ''.join(re.findall(r'"([^"\n]*)"', (ROOT / 'main/netsec/ui.c').read_text()))
    missing_fixed = {ord(c) for c in fixed if ord(c) >= 0x80 and ord(c) not in encoded}
    if missing_fixed:
        raise SystemExit(f'Missing UI glyphs: {sorted(missing_fixed)}')
    assert 0x1F600 not in encoded  # negative coverage check
    manifest = {'converter': 'lv_font_conv 1.5.3', 'font': 'Noto Sans CJK SC Regular',
                'size': 14, 'bpp': 2, 'glyphs': len(encoded),
                'ranges': RANGES, 'unassigned_or_missing': sorted(missing),
                'generated_sha256': hashlib.sha256(output.read_bytes()).hexdigest()}
    (ROOT / 'assets/fonts/netsec-font.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'NetSec font: PASS ({len(encoded)} glyphs; all Han and UI characters)')

if __name__ == '__main__':
    main()
