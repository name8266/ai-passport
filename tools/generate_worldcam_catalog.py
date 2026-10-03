#!/usr/bin/env python3
"""Generate or validate the flash-resident WorldCam catalogue."""
import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CAMERAS_PATH = ROOT / "gateway/cameras.json"
LOCATIONS_PATH = ROOT / "gateway/locations.json"
OUTPUT = ROOT / "main/worldcam_catalog.c"
HEADER = ROOT / "main/worldcam_catalog.h"


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
    return "\n".join(lines), cameras, locations


def semantic_check(cameras, locations):
    current = OUTPUT.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")
    if not re.search(rf"#define WC_CAMERA_COUNT {len(cameras)}\b", header):
        raise SystemExit("WC_CAMERA_COUNT does not match cameras.json")
    if not re.search(rf"#define WC_LOCATION_COUNT {len(locations)}\b", header):
        raise SystemExit("WC_LOCATION_COUNT does not match locations.json")

    camera_section, location_section = current.split("const wc_location_t wc_locations", 1)
    camera_rows = sum(1 for line in camera_section.splitlines() if line.startswith("    { "))
    location_rows = sum(1 for line in location_section.splitlines() if line.startswith("    { "))
    if camera_rows != len(cameras) or location_rows != len(locations):
        raise SystemExit(f"catalogue row count mismatch: {camera_rows}/{location_rows}")

    missing_urls = [c["id"] for c in cameras if cstr(c.get("snapshot", "")) not in camera_section]
    if missing_urls:
        raise SystemExit("missing camera URLs: " + ", ".join(missing_urls[:8]))

    missing_locations = []
    for location in locations:
        marker = f'{cstr(location["name_zh"])}, {cstr(location["country_zh"])}'
        if marker not in location_section:
            missing_locations.append(location.get("id", location["name_zh"]))
    if missing_locations:
        raise SystemExit("missing location labels: " + ", ".join(map(str, missing_locations[:8])))

    expected_usap = sum(c.get("resolver") == "usap" for c in cameras)
    if camera_section.count("WC_RESOLVER_USAP") != expected_usap:
        raise SystemExit("USAP resolver count mismatch")

    print(f"WorldCam catalogue semantics: PASS ({len(cameras)} cameras, {len(locations)} locations)")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    content, cameras, locations = build()
    if args.check:
        semantic_check(cameras, locations)
        return
    OUTPUT.write_text(content, encoding="utf-8")
    print(f"WorldCam catalogue generated: {len(cameras)} cameras, {len(locations)} locations")


if __name__ == "__main__":
    main()
