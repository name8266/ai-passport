#!/usr/bin/env python3
"""Build the fixed-size RGB565 Waite-Smith card atlas used by the firmware.

By default the script downloads public-domain 1909 scans from Wikimedia
Commons.  ``--source-dir`` accepts the pinned sixseeds/tarot-api mirror when
Commons is rate limited.  Modern recolourings are deliberately not accepted.
"""

from __future__ import annotations

import argparse
import hashlib
import http.client
import json
import struct
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

from PIL import Image, ImageOps


API = "https://commons.wikimedia.org/w/api.php"
CATEGORY = "Category:Rider-Waite tarot deck (Roses & Lilies)"
WIDTH = 112
HEIGHT = 192
USER_AGENT = "FoloToy-AI-Passport-Tarot/1.0 (public-domain asset build)"
MIRROR_REPOSITORY = "https://github.com/sixseeds/tarot-api"
MIRROR_COMMIT = "71825eed74683305b139a669b23ca5dc12f76857"


def expected_titles() -> list[str]:
    titles = [f"File:RWS1909 - {number:02d} {name}.jpeg" for number, name in enumerate([
        "Fool", "Magician", "High Priestess", "Empress", "Emperor", "Hierophant",
        "Lovers", "Chariot", "Strength", "Hermit", "Wheel of Fortune", "Justice",
        "Hanged Man", "Death", "Temperance", "Devil", "Tower", "Star", "Moon",
        "Sun", "Judgement", "World",
    ])]
    for suit in ("Wands", "Cups", "Swords", "Pentacles"):
        titles.extend(f"File:RWS1909 - {suit} {number:02d}.jpeg" for number in range(1, 15))
    return titles


def mirror_filenames() -> list[str]:
    names = [f"ar{number:02d}.jpg" for number in range(22)]
    for prefix in ("wa", "cu", "sw", "pe"):
        names.extend(f"{prefix}{number:02d}.jpg" for number in range(1, 15))
    return names


def request_json(params: dict[str, str]) -> dict:
    url = API + "?" + urllib.parse.urlencode(params)
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    for attempt in range(6):
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                return json.load(response)
        except (urllib.error.HTTPError, urllib.error.URLError,
                http.client.IncompleteRead, json.JSONDecodeError, OSError):
            if attempt == 5:
                raise
            time.sleep(2 ** attempt)
    raise RuntimeError("unreachable metadata retry state")


def fetch_metadata(titles: list[str]) -> dict[str, dict]:
    result: dict[str, dict] = {}
    for start in range(0, len(titles), 40):
        data = request_json({
            "action": "query",
            "format": "json",
            "prop": "imageinfo",
            "iiprop": "url|sha1|extmetadata",
            # 250 px is a Wikimedia pre-rendered thumbnail size. Using an
            # arbitrary width forces on-demand thumbnail work and triggers
            # the service's anti-abuse limits during a full-deck build.
            "iiurlwidth": "250",
            "titles": "|".join(titles[start:start + 40]),
        })
        for page in data["query"]["pages"].values():
            if "missing" in page or not page.get("imageinfo"):
                raise RuntimeError(f"missing Commons file: {page.get('title')}")
            result[page["title"]] = page["imageinfo"][0]
    return result


def download(url: str, destination: Path) -> bytes:
    if destination.exists():
        return destination.read_bytes()
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    for attempt in range(7):
        try:
            with urllib.request.urlopen(request, timeout=90) as response:
                data = response.read()
            break
        except (urllib.error.HTTPError, urllib.error.URLError,
                http.client.IncompleteRead, OSError) as error:
            if isinstance(error, urllib.error.HTTPError) and error.code != 429:
                raise
            if attempt == 6:
                raise
            retry_after = int(getattr(error, "headers", {}).get("Retry-After", "0") or 0)
            time.sleep(max(retry_after, 2 ** attempt))
    destination.write_bytes(data)
    time.sleep(0.35)
    return data


def rgb565_bytes(image: Image.Image) -> bytes:
    canvas = ImageOps.fit(image.convert("RGB"), (WIDTH, HEIGHT), Image.Resampling.LANCZOS)
    out = bytearray(WIDTH * HEIGHT * 2)
    offset = 0
    for red, green, blue in canvas.getdata():
        value = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)
        struct.pack_into("<H", out, offset, value)
        offset += 2
    return bytes(out)


def metadata_value(info: dict, key: str) -> str:
    return str(info.get("extmetadata", {}).get(key, {}).get("value", ""))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cache", type=Path, default=Path("/tmp/folotoy-tarot-source"))
    parser.add_argument("--source-dir", type=Path,
                        help="directory containing the pinned tarot-api cards/*.jpg mirror")
    parser.add_argument("--atlas", type=Path, default=Path("main/assets/tarot_cards.rgb565"))
    parser.add_argument("--manifest", type=Path, default=Path("assets/tarot/source-manifest.json"))
    args = parser.parse_args()

    titles = expected_titles()
    if len(titles) != 78 or len(set(titles)) != 78:
        raise RuntimeError("the deck definition must contain exactly 78 unique cards")
    filenames = mirror_filenames()
    metadata = None if args.source_dir else fetch_metadata(titles)
    args.cache.mkdir(parents=True, exist_ok=True)
    args.atlas.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.parent.mkdir(parents=True, exist_ok=True)

    atlas = bytearray()
    records = []
    for card_id, title in enumerate(titles):
        if args.source_dir:
            source_path = args.source_dir / filenames[card_id]
            if not source_path.is_file():
                raise RuntimeError(f"missing mirror card: {source_path}")
            source = source_path.read_bytes()
            record = {
                "card_id": card_id,
                "title": title,
                "source_file": filenames[card_id],
                "source_url": (
                    f"{MIRROR_REPOSITORY}/blob/{MIRROR_COMMIT}/cards/{filenames[card_id]}"
                ),
                "download_sha256": hashlib.sha256(source).hexdigest(),
                "license": "Public domain (Rider-Waite deck, 1909)",
                "artist": "Pamela Colman Smith",
                "credit": "Pinned mirror of the public-domain 1909 Rider-Waite deck",
            }
        else:
            assert metadata is not None
            info = metadata[title]
            license_name = metadata_value(info, "LicenseShortName")
            usage_terms = metadata_value(info, "UsageTerms")
            if "public domain" not in (license_name + " " + usage_terms).lower():
                raise RuntimeError(f"card is not marked public domain: {title} ({license_name})")
            url = info.get("thumburl") or info["url"]
            source = download(url, args.cache / f"{card_id:02d}.jpg")
            record = {
                "card_id": card_id,
                "title": title,
                "commons_page": info["descriptionurl"],
                "thumbnail_url": url,
                "commons_sha1": info.get("sha1", ""),
                "download_sha256": hashlib.sha256(source).hexdigest(),
                "license": license_name,
                "artist": metadata_value(info, "Artist"),
                "credit": metadata_value(info, "Credit"),
            }
        from io import BytesIO
        with Image.open(BytesIO(source)) as image:
            converted = rgb565_bytes(image)
        if len(converted) != WIDTH * HEIGHT * 2:
            raise RuntimeError(f"unexpected converted size for {title}")
        atlas.extend(converted)
        records.append(record)

    args.atlas.write_bytes(atlas)
    manifest = {
        "schema": 1,
        "source_category": CATEGORY if not args.source_dir else None,
        "source_repository": MIRROR_REPOSITORY if args.source_dir else None,
        "source_commit": MIRROR_COMMIT if args.source_dir else None,
        "card_count": len(records),
        "width": WIDTH,
        "height": HEIGHT,
        "pixel_format": "RGB565 little-endian",
        "bytes_per_card": WIDTH * HEIGHT * 2,
        "atlas_size": len(atlas),
        "atlas_sha256": hashlib.sha256(atlas).hexdigest(),
        "cards": records,
    }
    args.manifest.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {len(records)} cards, {len(atlas)} bytes, sha256={manifest['atlas_sha256']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
