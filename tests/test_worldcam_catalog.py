#!/usr/bin/env python3
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
MAIN = (ROOT / "main/main.c").read_text(encoding="utf-8")
README = (ROOT / "README.md").read_text(encoding="utf-8")
CATALOG_H = (ROOT / "main/worldcam_catalog.h").read_text(encoding="utf-8")

subprocess.run([sys.executable, str(ROOT / "tools/generate_worldcam_catalog.py"), "--check"], check=True)

assert re.search(r"#define WC_CAMERA_COUNT 825\b", CATALOG_H)
assert re.search(r"#define WC_LOCATION_COUNT 1020\b", CATALOG_H)
for forbidden in ("/api/index", "/api/location", "/api/frame-location", "s_gateway", "wc_gateway_valid"):
    assert forbidden not in MAIN, forbidden
for expected in (
    'Accept-Encoding", "identity"',
    ".disable_auto_redirect = true",
    "camera_scenic_tier",
    "保留上一帧",
):
    assert expected in MAIN, expected
assert "No PC, Raspberry Pi, proxy service, or local gateway server is required." in README
assert "gateway address" not in MAIN.lower()
print("WorldCam direct-device contract: PASS")
