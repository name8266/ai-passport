#!/usr/bin/env python3
"""Build and check the fixed Chinese UI subset for Star Orbit.

The generated C files are checked in; firmware builds never download fonts.
Requires Node/npx only when regenerating. --check uses Python's standard library.
"""

import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "main/main.c"
DEST = ROOT / "assets/fonts"
FONT_URL = ("https://raw.githubusercontent.com/adobe-fonts/source-han-sans/"
            "a4f7cf94edfb9d7ffbdfc4841de276358bd7e0f2/OTF/"
            "SimplifiedChinese/SourceHanSansSC-Regular.otf")
FONT_SHA256 = "f1d8611151880c6c336aabeac4640ef434fa13cbfbf1ffe82d0a71b2a5637256"
SIZES = (14, 16, 20)


def ui_codepoints():
    text = SOURCE.read_text(encoding="utf-8")
    literals = re.findall(r'"(?:\\.|[^"\\])*"', text)
    return {ord(ch) for literal in literals for ch in literal if ord(ch) > 127}


def check():
    required = set(range(0x20, 0x7F)) | ui_codepoints()
    for size in SIZES:
        path = DEST / f"zuma_cjk_{size}.c"
        text = path.read_text(encoding="utf-8")
        actual = {int(value, 16) for value in re.findall(r"/\* U\+([0-9A-Fa-f]+) ", text)}
        missing = required - actual
        if missing:
            raise SystemExit(f"{path}: missing glyphs " +
                             ", ".join(f"U+{cp:04X}" for cp in sorted(missing)))
        if 0x9F98 in actual:  # A known non-UI character guards against vacuous checks.
            raise SystemExit(f"{path}: unexpected U+9F98")
        print(f"{path.relative_to(ROOT)}: {len(required)} UI/ASCII glyphs covered")


def download_font(path):
    if not path.exists():
        urllib.request.urlretrieve(FONT_URL, path)
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != FONT_SHA256:
        raise SystemExit(f"Source font hash mismatch: {digest}")


def generate(source):
    chinese = "".join(chr(cp) for cp in sorted(ui_codepoints()))
    for size in SIZES:
        path = DEST / f"zuma_cjk_{size}.c"
        subprocess.run([
            "npx", "--yes", "lv_font_conv@1.5.3",
            "--font", str(source), "--range", "0x20-0x7E",
            "--symbols", chinese, "--size", str(size), "--bpp", "4",
            "--format", "lvgl", "--no-compress", "--no-kerning",
            "--lv-font-name", f"zuma_cjk_{size}", "--lv-include", "lvgl.h",
            "--output", str(path),
        ], check=True)
        path.write_text(path.read_text(encoding="utf-8").rstrip() + "\n", encoding="utf-8")
    check()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--font-source", type=Path)
    args = parser.parse_args()
    if args.check:
        check()
        return
    DEST.mkdir(exist_ok=True)
    if args.font_source:
        download_font(args.font_source)
        generate(args.font_source)
    else:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "SourceHanSansSC-Regular.otf"
            download_font(source)
            generate(source)


if __name__ == "__main__":
    main()
