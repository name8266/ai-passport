<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, music, and sound effects, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Brake Hero assets

- `images/brake-hero/source/recovered/`: 13 display-sized indexed PNG inputs recovered losslessly from full-image SHA256 `6128ab20f18f0db50d13d2aab459975abfb85579771b33bf0c175f5b4f939940`. The historical generation prompts remain in `source/`; original large atlases were unavailable in this local repair. Converted C arrays and final palette are reproduced by `tools/convert_brake_assets.py`. Sixteen palette entries hold the procedural vehicle materials. No photographic car asset is embedded.
- `fonts/brake-hero/`: Noto Sans CJK SC subset OTF, SIL OFL license, 14 px bitmap C font and exact character inventory. The source font is from <https://github.com/notofonts/noto-cjk>. Regenerate with `python tools/convert_brake_assets.py`; when adding characters use `--font-source` with the full upstream SC font. Missing glyphs fail conversion. The renderer uses this bitmap font directly; arbitrary Unicode player names are unsupported.
- `music/brake-hero/`: eight Mandarin formant-synthesized announcements and four collision effects restored from the same verified image, with decoded 16 kHz PCM16 WAV previews and byte-identical PCM8 Flash arrays. The original eSpeak NG 1.52.0 processing is described in `tools/generate_brake_audio.py`; no performer recording or voice clone is used. Recovered WAVs reflect the shipped PCM8 signal rather than the unavailable higher-precision synthesis inputs.
- `main/brake_vehicle.c`: original code-native MPV mesh, with visual reference to <https://hima.auto/zunjie/v800/>. It implements animated tent fabric/shell, brake lamps and game impact deformation; it is not manufacturer CAD or real crash data. The official reference photograph is not redistributed.

Generated C resources are compiled by `main/CMakeLists.txt`. Asset regeneration uses `tools/brake_asset_requirements.txt`; firmware compilation needs none of those Python packages. See the [engineering notes](../docs/apps/brake-hero.md).

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
