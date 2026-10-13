#!/usr/bin/env python3
"""Rehydrate AI-generated docking artwork into Flash-resident LVGL pixel assets.

The committed *.b64 sources are zlib-compressed, downsampled derivatives of
the original generated background and generated transparent sprite sheet.
Conversion runs during CI and firmware build; no PIL or Node dependency.
The 60x80 background is bilinearly enlarged to the Passport's 240x320 screen.
"""
import base64
import hashlib
from pathlib import Path
import zlib

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/images/eight-realms"
TARGET = ROOT / "main/art/eight-realms"

SOURCES = {
    "docking-bg": (4944, "8cbda2fb889900c45b9ca126de5961465f47c96a141e8fc90230bb08415f3fdd"),
    "docking-port": (6912, "3c0f52678b6f397954a726fcea7766398edc3b8e95c87756f95a53ca6266dc7d"),
    "docking-ship": (1440, "a9f7a7c3215f1bfb94141ba06dcd80a791366da6a85e078513277ce56d884d5f"),
}


def read_source(name):
    expected_length, expected_sha256 = SOURCES[name]
    encoded = "".join((SOURCE / f"{name}.b64").read_text(encoding="ascii").split())
    raw = zlib.decompress(base64.urlsafe_b64decode(encoded))
    digest = hashlib.sha256(raw).hexdigest()
    if len(raw) != expected_length or digest != expected_sha256:
        raise SystemExit(f"{name}: generated art integrity mismatch; refusing firmware build")
    return raw


def create_rgb565_background(raw):
    palette = raw[:144]
    indices = raw[144:]
    if len(indices) != 60 * 80:
        raise SystemExit("invalid docking background dimensions")
    # Palette indexes are RGB888; output is little-endian RGB565 in Flash.
    pixels = bytearray(240 * 320 * 2)
    for y in range(320):
        yf = max(0.0, min(79.0, (y + 0.5) * 80 / 320 - 0.5))
        y0 = int(yf); y1 = min(79, y0 + 1); wy = yf - y0
        for x in range(240):
            xf = max(0.0, min(59.0, (x + 0.5) * 60 / 240 - 0.5))
            x0 = int(xf); x1 = min(59, x0 + 1); wx = xf - x0
            t0 = indices[y0 * 60 + x0] * 3
            t1 = indices[y0 * 60 + x1] * 3
            t2 = indices[y1 * 60 + x0] * 3
            t3 = indices[y1 * 60 + x1] * 3
            channels = []
            for ch in range(3):
                top = palette[t0 + ch] * (1 - wx) + palette[t1 + ch] * wx
                bottom = palette[t2 + ch] * (1 - wx) + palette[t3 + ch] * wx
                channels.append(int(top * (1 - wy) + bottom * wy + 0.5))
            red, green, blue = channels
            value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
            i = (y * 240 + x) * 2
            pixels[i] = value & 255
            pixels[i + 1] = value >> 8
    return bytes(pixels)


def main():
    TARGET.mkdir(parents=True, exist_ok=True)
    background = create_rgb565_background(read_source("docking-bg"))
    assert len(background) == 153600
    (TARGET / "docking.rgb565").write_bytes(background)
    for name, target in [("docking-port", "docking-sprite.rgb565a8"),
                         ("docking-ship", "docking-ship.rgb565a8")]:
        raw = read_source(name)
        (TARGET / target).write_bytes(raw)
    print("Docking AI artwork: PASS (240x320 RGB565, 48x48 + 24x20 RGB565A8)")


if __name__ == "__main__":
    main()
