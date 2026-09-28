#!/usr/bin/env python3
"""Validate the deterministic firmware deck and Chinese font inputs."""

from __future__ import annotations

import hashlib
import json
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
    assert all("public domain" in item["license"].lower() for item in manifest["cards"])
    assert manifest["bytes_per_card"] == 112 * 192 * 2
    assert manifest["atlas_size"] == 78 * 112 * 192 * 2
    assert len(atlas) == manifest["atlas_size"]
    assert hashlib.sha256(atlas).hexdigest() == manifest["atlas_sha256"]

    font_manifest = json.loads((ROOT / "assets" / "fonts" / "tarot-font-manifest.json").read_text(encoding="utf-8"))
    source_text = "".join(path.read_text(encoding="utf-8") for path in [
        *sorted((ROOT / "main").glob("tarot_*.c")), ROOT / "main" / "main.c",
    ])
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
