#!/usr/bin/env python3
"""Generate reproducible LVGL Chinese subsets for the tarot application."""

from __future__ import annotations

import hashlib
import json
import re
import subprocess
import urllib.request
from pathlib import Path


FONT_URL = "https://raw.githubusercontent.com/google/fonts/main/ofl/notosanssc/NotoSansSC%5Bwght%5D.ttf"
LICENSE_URL = "https://raw.githubusercontent.com/google/fonts/main/ofl/notosanssc/OFL.txt"
FONT_CONV = "lv_font_conv@1.5.3"
USER_AGENT = "FoloToy-AI-Passport-Tarot/1.0 (font asset build)"


def fetch(url: str, target: Path) -> bytes:
    if target.exists():
        return target.read_bytes()
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=180) as response:
        data = response.read()
    target.write_bytes(data)
    return data


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    cache = Path("/tmp/folotoy-tarot-font")
    cache.mkdir(parents=True, exist_ok=True)
    font_path = cache / "NotoSansSC-wght.ttf"
    license_path = cache / "OFL.txt"
    font_data = fetch(FONT_URL, font_path)
    license_data = fetch(LICENSE_URL, license_path)

    text = ""
    for path in sorted((root / "main").glob("tarot_*.c")) + [root / "main" / "main.c"]:
        text += path.read_text(encoding="utf-8")
    symbols = "".join(sorted({character for character in text if ord(character) > 0x7F and not character.isspace()}))
    if not symbols:
        raise RuntimeError("no non-ASCII application symbols found")

    output_dir = root / "main" / "fonts"
    output_dir.mkdir(parents=True, exist_ok=True)
    npx = "/opt/homebrew/bin/npx"
    if not Path(npx).exists():
        npx = "npx"
    for size in (14, 20):
        output = output_dir / f"tarot_font_{size}.c"
        subprocess.run([
            npx, "--yes", FONT_CONV,
            "--font", str(font_path), "--size", str(size), "--bpp", "2",
            "--format", "lvgl", "--range", "0x20-0x7E", "--symbols", symbols,
            "--no-compress", "--output", str(output),
        ], check=True, cwd=root)
        generated = output.read_text(encoding="utf-8")
        generated = generated.replace(
            '#ifdef LV_LVGL_H_INCLUDE_SIMPLE\n#include "lvgl.h"\n#else\n#include "lvgl/lvgl.h"\n#endif',
            '#include "lvgl.h"',
            1,
        )
        output.write_text(generated, encoding="utf-8")

    manifest = {
        "schema": 1,
        "font": "Noto Sans SC",
        "font_url": FONT_URL,
        "font_sha256": hashlib.sha256(font_data).hexdigest(),
        "license": "SIL Open Font License 1.1",
        "license_url": LICENSE_URL,
        "license_sha256": hashlib.sha256(license_data).hexdigest(),
        "converter": FONT_CONV,
        "sizes": [14, 20],
        "bpp": 2,
        "ascii_range": "0x20-0x7E",
        "symbols": symbols,
        "symbol_count": len(symbols),
    }
    target = root / "assets" / "fonts" / "tarot-font-manifest.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    (target.parent / "OFL-NotoSansSC.txt").write_bytes(license_data)
    target.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"generated 14px/20px fonts with {len(symbols)} non-ASCII symbols")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
