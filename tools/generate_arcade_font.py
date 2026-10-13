#!/usr/bin/env python3
"""Regenerate and verify the exact Chinese glyph subset used by Eight Realms."""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile

from generate_star_orbit_font import download_font

ROOT = Path(__file__).resolve().parents[1]
SOURCES = (ROOT / "main/arcade_main.c", ROOT / "main/arcade_game.c")
DEST = ROOT / "assets/fonts"
SIZES = (14, 20)


def required_codepoints():
    strings = "".join(literal for path in SOURCES
                      for literal in re.findall(r'"(?:\\.|[^"\\])*"',
                                                path.read_text(encoding="utf-8")))
    return set(range(0x20, 0x7f)) | {ord(ch) for ch in strings if ord(ch) > 127}


def check():
    needed = required_codepoints()
    for size in SIZES:
        path = DEST / f"arcade_cjk_{size}.c"
        actual = {int(cp, 16) for cp in re.findall(r"/\* U\+([0-9A-Fa-f]+) ",
                  path.read_text(encoding="utf-8"))}
        missing = needed - actual
        if missing:
            raise SystemExit(f"{path}: missing " + ", ".join(f"U+{x:04X}" for x in sorted(missing)))
        print(f"{path.relative_to(ROOT)}: {len(needed)} UI/ASCII glyphs covered")


def generate(font):
    symbols = "".join(chr(cp) for cp in sorted(required_codepoints()) if cp > 127)
    for size in SIZES:
        subprocess.run([
            "npx", "--yes", "lv_font_conv@1.5.3",
            "--font", str(font), "--range", "0x20-0x7E", "--symbols", symbols,
            "--size", str(size), "--bpp", "4", "--format", "lvgl",
            "--no-compress", "--no-kerning", "--lv-font-name", f"arcade_cjk_{size}",
            "--lv-include", "lvgl.h", "--output", str(DEST / f"arcade_cjk_{size}.c")
        ], check=True)
        path = DEST / f"arcade_cjk_{size}.c"
        path.write_text(path.read_text(encoding="utf-8").rstrip() + "\n", encoding="utf-8")
    check()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if args.check: check()
    else:
        DEST.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory() as directory:
            font = Path(directory) / "SourceHanSansSC-Regular.otf"
            download_font(font)
            generate(font)
