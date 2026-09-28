<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Tarot card assets

The firmware uses original 1909 Waite-Smith scans. The checked-in atlas was
generated from the `sixseeds/tarot-api` mirror pinned to commit
`71825eed74683305b139a669b23ca5dc12f76857`; its README identifies all 78
Rider-Waite images as public domain. The generator can also retrieve the
"Roses & Lilies" scans directly from Wikimedia Commons. Modern recolourings
are intentionally excluded.

`source-manifest.json` records every source page, source hash, downloaded-file
hash, attribution field, license label, card order, generated dimensions, and
the final atlas hash. Regenerate the checked-in RGB565 atlas with:

```bash
python3 tools/gen_tarot_assets.py
```

To reproduce the checked-in atlas from a local checkout of the pinned mirror:

```bash
python3 tools/gen_tarot_assets.py --source-dir /path/to/tarot-api/cards
```

The generator requires Pillow. It creates one 112 x 192 little-endian RGB565
frame per card and concatenates the 78 frames into
`main/assets/tarot_cards.rgb565`. The fixed stride lets the firmware address a
card directly from flash without allocating or decoding a full image in RAM.
