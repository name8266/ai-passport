#!/usr/bin/env python3
"""Convert the original generated art into flash-resident LVGL image data.

Requires Pillow. This is deterministic resizing/compositing for the 240x320
display; it does not generate or retouch the source illustrations.
"""

from pathlib import Path
import re
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/images/star-orbit"
DEST = ROOT / "main/art"
SCALE = 4
ROUTE_RE = re.compile(r"\{(\d+),\s*(\d+)\}")


def rgb565_bytes(image):
    out = bytearray()
    for r, g, b in image.convert("RGB").getdata():
        v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        out.extend((v & 255, v >> 8))
    return out


def save_565a8(image, path):
    image = image.convert("RGBA")
    rgb = rgb565_bytes(image)
    alpha = bytes(a for _, _, _, a in image.getdata())
    path.write_bytes(rgb + alpha)
    return len(rgb) + len(alpha)


def path_points():
    code = (ROOT / "main/zuma_game.c").read_text()
    section = code.split("static const point_t route[] = {", 1)[1].split("};", 1)[0]
    route = [(int(x), int(y)) for x, y in ROUTE_RE.findall(section)]
    points = []
    for i in range(len(route) - 1):
        p0, p1, p2, p3 = route[max(i - 1, 0)], route[i], route[i + 1], route[min(i + 2, len(route) - 1)]
        for j in range(12):
            t = j / 12
            t2, t3 = t * t, t * t * t
            x = .5 * (2 * p1[0] + (-p0[0] + p2[0]) * t + (2*p0[0] - 5*p1[0] + 4*p2[0] - p3[0]) * t2 + (-p0[0] + 3*p1[0] - 3*p2[0] + p3[0]) * t3)
            y = .5 * (2 * p1[1] + (-p0[1] + p2[1]) * t + (2*p0[1] - 5*p1[1] + 4*p2[1] - p3[1]) * t2 + (-p0[1] + 3*p1[1] - 3*p2[1] + p3[1]) * t3)
            points.append((round(x * SCALE), round(y * SCALE)))
    points.append((route[-1][0] * SCALE, route[-1][1] * SCALE))
    return points


def make_board():
    board = Image.open(SOURCE / "temple-source.png").convert("RGB").resize((240*SCALE, 320*SCALE), Image.Resampling.LANCZOS).convert("RGBA")
    pts = path_points()
    overlay = Image.new("RGBA", board.size)
    draw = ImageDraw.Draw(overlay)
    # A recessed carved stone trough; all layers follow the exact collision path.
    shadow_pts = [(x + 3, y + 5) for x, y in pts]
    draw.line(shadow_pts, fill=(0, 0, 0, 175), width=22*SCALE, joint="curve")
    draw.line(pts, fill=(104, 76, 40, 215), width=18*SCALE, joint="curve")
    draw.line(pts, fill=(9, 21, 22, 245), width=15*SCALE, joint="curve")
    draw.line(pts, fill=(34, 44, 40, 240), width=11*SCALE, joint="curve")
    draw.line(pts, fill=(75, 67, 49, 105), width=2*SCALE, joint="curve")
    # Small burnished studs make the carved track legible in sparse areas.
    for x, y in pts[8:-8:24]:
        r = 2*SCALE
        draw.ellipse((x-r, y-r, x+r, y+r), fill=(170, 120, 53, 195))
        draw.ellipse((x-r//2, y-r//2, x+r//2, y+r//2), fill=(43, 62, 56, 230))
    # Eclipse endpoint on the path, with a turquoise inner light.
    gx, gy = pts[-1]
    for radius, color in [(18, (0, 213, 207, 45)), (14, (188, 130, 49, 210)), (11, (9, 25, 25, 255)), (7, (23, 100, 94, 255))]:
        r = radius*SCALE
        draw.ellipse((gx-r, gy-r, gx+r, gy+r), fill=color)
    r = 4*SCALE
    draw.ellipse((gx-r, gy-r, gx+r, gy+r), fill=(92, 224, 210, 230))
    board = Image.alpha_composite(board, overlay).convert("RGB").resize((240, 320), Image.Resampling.LANCZOS)
    board.save(SOURCE / "board-preview.png")
    (DEST / "board.rgb565").write_bytes(rgb565_bytes(board))


def make_guardian():
    source = Image.open(SOURCE / "guardian-source.png").convert("RGBA")
    bounds = source.getchannel("A").getbbox()
    source = source.crop(bounds)
    source.thumbnail((72, 72), Image.Resampling.LANCZOS)
    sprite = Image.new("RGBA", (72, 72))
    sprite.alpha_composite(source, ((72-source.width)//2, (72-source.height)//2))
    sprite.save(SOURCE / "guardian-preview.png")
    save_565a8(sprite, DEST / "guardian.rgb565a8")


def make_marbles():
    sheet = Image.open(SOURCE / "marbles-source.png").convert("RGBA")
    w, h = sheet.size
    for i, (cx, cy) in enumerate(((.25,.25),(.75,.25),(.25,.75),(.75,.75))):
        size = int(min(w,h) * .48)
        x, y = int(w*cx), int(h*cy)
        crop = sheet.crop((x-size//2, y-size//2, x+size//2, y+size//2))
        sprite = crop.resize((14, 14), Image.Resampling.LANCZOS)
        sprite.save(SOURCE / f"marble-{i}-preview.png")
        save_565a8(sprite, DEST / f"marble-{i}.rgb565a8")


def main():
    DEST.mkdir(exist_ok=True)
    make_board()
    make_guardian()
    make_marbles()
    for path in DEST.iterdir():
        print(path.relative_to(ROOT), path.stat().st_size)


if __name__ == "__main__":
    main()
