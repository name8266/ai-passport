#!/usr/bin/env python3
"""Validate the deterministic firmware deck and Chinese font inputs."""

from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    manifest_path = ROOT / "assets" / "tarot" / "source-manifest.json"
    atlas_path = ROOT / "main" / "assets" / "tarot_cards.rgb565"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    atlas = atlas_path.read_bytes()
    assert manifest["card_count"] == 78
    assert len(manifest["cards"]) == 78
    assert [item["card_id"] for item in manifest["cards"]] == list(range(78))
    assert len({item["title"] for item in manifest["cards"]}) == 78
    expected_sources = [f"ar{card:02d}.jpg" for card in range(22)]
    expected_sources += [
        f"{suit}{rank:02d}.jpg"
        for suit in ("wa", "cu", "sw", "pe")
        for rank in range(1, 15)
    ]
    assert [item["source_file"] for item in manifest["cards"]] == expected_sources
    assert len({item["download_sha256"] for item in manifest["cards"]}) == 78
    assert all("public domain" in item["license"].lower() for item in manifest["cards"])
    assert manifest["bytes_per_card"] == 112 * 192 * 2
    assert manifest["atlas_size"] == 78 * 112 * 192 * 2
    assert len(atlas) == manifest["atlas_size"]
    assert hashlib.sha256(atlas).hexdigest() == manifest["atlas_sha256"]

    font_manifest = json.loads((ROOT / "assets" / "fonts" / "tarot-font-manifest.json").read_text(encoding="utf-8"))
    source_paths = [
        *sorted((ROOT / "main").glob("tarot_*.c")), ROOT / "main" / "main.c",
    ]
    source_text = "".join(path.read_text(encoding="utf-8") for path in source_paths)

    catalog_text = (ROOT / "main" / "tarot_catalog.c").read_text(encoding="utf-8")

    def string_array(name: str) -> list[str]:
        match = re.search(
            rf"static const char \*const {name}\[\] = \{{(.*?)\}};",
            catalog_text,
            re.DOTALL,
        )
        assert match, f"missing catalog array {name}"
        return re.findall(r'"([^"\\]*(?:\\.[^"\\]*)*)"', match.group(1))

    major_names = string_array("MAJOR_NAMES")
    suits = string_array("SUITS")
    ranks = string_array("RANKS")
    catalog_names = major_names + [f"{suit}{rank}" for suit in suits for rank in ranks]
    assert len(major_names) == 22
    assert len(suits) == 4
    assert len(ranks) == 14
    assert len(catalog_names) == 78
    assert len(set(catalog_names)) == 78

    required = {character for character in source_text if ord(character) > 0x7F and not character.isspace()}
    assert required <= set(font_manifest["symbols"])
    for size in font_manifest["sizes"]:
        generated = ROOT / "main" / "fonts" / f"tarot_font_{size}.c"
        text = generated.read_text(encoding="utf-8")
        assert f"tarot_font_{size}" in text
        assert len(text) > 1000
    print("tarot assets: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
