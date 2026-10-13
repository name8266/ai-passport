#!/usr/bin/env python3
"""Crop the generated eight-scene atlas into Flash-resident RGB565 backgrounds."""

from pathlib import Path
from PIL import Image, ImageEnhance

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/images/eight-realms/atlas.png"
SPRITES = ROOT / "assets/images/eight-realms/sprites-atlas.png"
NAMES = ("breakout", "tetris", "doodle", "snake", "shooter", "flappy", "lander", "racer")
SPRITE_SIZES = ((60, 18), (15, 13), (22, 26), (18, 18),
                (30, 29), (24, 20), (24, 24), (28, 36))


def main():
    image = Image.open(SOURCE).convert("RGB")
    if image.size != (1448, 1086):
        raise SystemExit(f"Unexpected source atlas size: {image.size}")
    asset_dir = ROOT / "assets/images/eight-realms"
    binary_dir = ROOT / "main/art/eight-realms"
    binary_dir.mkdir(parents=True, exist_ok=True)
    previews = []
    for i, name in enumerate(NAMES):
        x = (i % 4) * 362
        y = (i // 4) * 543
        # Crop equal margins vertically to match the Passport's 3:4 panel.
        tile = image.crop((x, y + 30, x + 362, y + 513))
        tile = tile.resize((240, 320), Image.Resampling.LANCZOS)
        tile = ImageEnhance.Brightness(tile).enhance(0.76)
        tile.save(asset_dir / f"{name}.png", optimize=True)
        previews.append(tile)
        raw = bytearray()
        for red, green, blue in tile.getdata():
            value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
            raw.extend((value & 255, value >> 8))
        (binary_dir / f"{name}.rgb565").write_bytes(raw)
    overview = Image.new("RGB", (960, 640))
    for i, tile in enumerate(previews):
        overview.paste(tile, ((i % 4) * 240, (i // 4) * 320))
    overview.save(asset_dir / "overview.png", optimize=True)
    atlas = Image.open(SPRITES).convert("RGBA")
    if atlas.size != (1774, 887):
        raise SystemExit(f"Unexpected sprite atlas size: {atlas.size}")
    for i, (name, size) in enumerate(zip(NAMES, SPRITE_SIZES)):
        left = round((i % 4) * atlas.width / 4)
        right = round(((i % 4) + 1) * atlas.width / 4)
        top = round((i // 4) * atlas.height / 2)
        bottom = round(((i // 4) + 1) * atlas.height / 2)
        cell = atlas.crop((left, top, right, bottom))
        bounds = cell.getbbox()
        if not bounds:
            raise SystemExit(f"Empty sprite cell: {name}")
        cell = cell.crop(bounds)
        cell.thumbnail(size, Image.Resampling.LANCZOS)
        sprite = Image.new("RGBA", size)
        sprite.alpha_composite(cell, ((size[0] - cell.width) // 2,
                                      (size[1] - cell.height) // 2))
        sprite.save(asset_dir / f"{name}-sprite.png", optimize=True)
        rgb = bytearray()
        alpha = bytearray()
        for red, green, blue, a in sprite.getdata():
            value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
            rgb.extend((value & 255, value >> 8))
            alpha.append(a)
        (binary_dir / f"{name}-sprite.rgb565a8").write_bytes(rgb + alpha)
    print(f"Generated {len(NAMES)} 240x320 RGB565 backgrounds ({len(NAMES) * 153600:,} bytes)")


if __name__ == "__main__":
    main()
