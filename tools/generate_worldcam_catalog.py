#!/usr/bin/env python3
"""Generate the flash-resident WorldCam catalogue from maintained JSON data."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CAMERAS_PATH = ROOT / "gateway/cameras.json"
LOCATIONS_PATH = ROOT / "gateway/locations.json"
OUTPUT = ROOT / "main/worldcam_catalog.c"


def cstr(value):
    return json.dumps(str(value), ensure_ascii=False)


def build():
    cameras = json.loads(CAMERAS_PATH.read_text(encoding="utf-8"))
    locations = json.loads(LOCATIONS_PATH.read_text(encoding="utf-8"))
    by_id = {camera["id"]: index for index, camera in enumerate(cameras)}

    lines = ['#include "worldcam_catalog.h"', "", "const wc_camera_t wc_cameras[WC_CAMERA_COUNT] = {"]
    for camera in cameras:
        resolver = "WC_RESOLVER_USAP" if camera.get("resolver") == "usap" else "WC_RESOLVER_NONE"
        lines.append(f'    {{ {cstr(camera.get("snapshot", ""))}, {resolver} }},')
    lines += ["};", "", "const wc_location_t wc_locations[WC_LOCATION_COUNT] = {"]
    for location in locations:
        index = by_id.get(location.get("camera_id"))
        camera = cameras[index] if index is not None else None
        flags = []
        if camera and camera.get("snapshot") and camera.get("status") != "unavailable":
            flags.append("WC_FLAG_AVAILABLE")
        if location.get("capital"):
            flags.append("WC_FLAG_CAPITAL")
        if camera:
            flags.append("WC_FLAG_HAS_SOURCE")
        flags_text = " | ".join(flags) if flags else "0"
        camera_text = str(index) if index is not None else "WC_CAMERA_NONE"
        lat = round(float(location["lat"]) * 100)
        lon = round(float(location["lon"]) * 100)
        lines.append(
            f'    {{ {lat}, {lon}, {camera_text}, {flags_text}, '
            f'{cstr(location["name_zh"])}, {cstr(location["country_zh"])} }},'
        )
    lines += ["};", ""]
    return "\n".join(lines), len(cameras), len(locations)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    content, cameras, locations = build()
    if args.check:
        current = OUTPUT.read_text(encoding="utf-8")
        if current != content:
            raise SystemExit("worldcam_catalog.c is stale; run tools/generate_worldcam_catalog.py")
        print(f"WorldCam catalogue: PASS ({cameras} cameras, {locations} locations)")
        return
    OUTPUT.write_text(content, encoding="utf-8")
    print(f"WorldCam catalogue generated: {cameras} cameras, {locations} locations")


if __name__ == "__main__":
    main()
