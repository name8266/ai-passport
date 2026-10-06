<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, music, and sound effects, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Fonts

Store reusable font files and generated font sources in `fonts/`.

- Use descriptive names that include the family, weight, size, and format when relevant.
- Document the source, license, character range, conversion command, and expected destination.
- Check Flash and internal-RAM impact before adding a font; the ESP32-C3 has no PSRAM.
- Do not commit fonts whose license does not permit redistribution.

## Images

Store reusable source images and generated display assets in `images/`.

| File | Dimensions and format | Use and source |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160, JPEG | Product hero image embedded in both project README files to foreground AI Passport and its open, maker-oriented identity. |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724, PNG RGBA | Optional technical infographic retained as a reference asset; it is no longer used as the homepage hero. Generated for this repository with the built-in image generation tool on 2026-09-17; the six labels and values were checked against the documented hardware contract. |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336, PNG RGBA | Transparent black wordmark extracted from the repository's original `images/logo.png`; embedded in both project README files for light backgrounds. |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336, PNG RGBA | White version of the extracted wordmark, used by the README `<picture>` element when GitHub is in dark mode. |

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.

### Arcade Chinese UI fonts

`fonts/NotoSansCJKsc-Arcade.otf` is a subset of [Noto Sans CJK SC Regular](https://github.com/notofonts/noto-cjk/blob/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf), licensed under SIL OFL 1.1 (retained in `fonts/OFL.txt`). It contains printable ASCII and the fixed application character inventory in `fonts/arcade_symbols.txt`. The 14px/20px, 4bpp, uncompressed `arcade_font_*.c` files are compiled into Flash by the main component. Regenerate with `python3 tools/regenerate_arcade_fonts.py --converter /path/to/lv_font_conv` using lv_font_conv 1.5.3. When adding characters, rebuild the OTF subset from the upstream font before converting. Startup checks both sizes and a known-missing negative glyph.

### Classic slot reel symbols

`images/slot-classic-atlas.png` is an original 1536×1024 RGBA six-symbol atlas generated with the built-in image generation tool on 2026-10-06 for this application. It uses traditional mechanical reel motifs (cherries, lemon, BAR, gold bell, purple plum and red 7); no third-party artwork or brand marks are embedded. The visual references were real mechanical reel examples, including the [1938 Mills Cherry Bell](https://www.antiguedades.es/es/otros-objetos-antiguos-vendidos/1154-tragaperras-antigua-para-mercado-ingles-del-ano-1938-en-funcionamiento-con-peniques-ingleses-para-su-manejo.html), without copying their artwork.

Regenerate firmware sprites with `python3 tools/convert_slot_symbols.py` (requires Pillow). The converter crops the six transparent cells, keeps each symbol's aspect ratio, centers it in 48×64 classic or 32×36 multiline ivory tiles, and encodes little-endian RGB565. `images/slot-*.png` retain native-size previews; `images/slot-symbols-native.png` is the 288×64 classic contact sheet; `images/slot-symbols-small-native.png` is the 192×36 multiline sheet. `images/slot_symbols.c` provides twelve shared const LVGL image descriptors, totaling 50,688 bytes of pixel data in Flash, with no per-row pixel copy or alpha layer. The existing `SLOT_GEM` ID now displays the plum; IDs, selection probabilities and outcome logic are unchanged. `tests/test_slot_symbols.py` checks dimensions, descriptor order, data lengths, uniqueness, contrast and tile corners.
