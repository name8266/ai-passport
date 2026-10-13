<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, music, and sound effects, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Fonts

Store reusable font files and generated font sources in `fonts/`.

| File | Use and source |
| --- | --- |
| [`fonts/zuma_cjk_14.c`](fonts/zuma_cjk_14.c), [`zuma_cjk_16.c`](fonts/zuma_cjk_16.c), [`zuma_cjk_20.c`](fonts/zuma_cjk_20.c) | Flash-resident 4-bpp LVGL subsets for the fixed Chinese Star Orbit UI, generated with `python3 tools/generate_star_orbit_font.py` and `lv_font_conv@1.5.3`. Source: Adobe Source Han Sans SC Regular at commit `a4f7cf94edfb9d7ffbdfc4841de276358bd7e0f2`, SHA-256 `f1d8611151880c6c336aabeac4640ef434fa13cbfbf1ffe82d0a71b2a5637256`. Coverage includes ASCII U+0020–U+007E and the non-ASCII characters in `main/main.c` string literals. `--check` verifies coverage without downloading. The subsets and [`OFL-SourceHanSans.txt`](fonts/OFL-SourceHanSans.txt) use SIL OFL 1.1. |
| [`fonts/arcade_cjk_14.c`](fonts/arcade_cjk_14.c), [`arcade_cjk_20.c`](fonts/arcade_cjk_20.c) | Flash-resident 4-bpp LVGL subsets for the Eight Realms UI. `python3 tools/generate_arcade_font.py` regenerates them with `lv_font_conv@1.5.3`; `--check` verifies the ASCII and Chinese characters in `main/arcade_main.c` and `main/arcade_game.c`. The Adobe Source Han Sans SC Regular source and SIL OFL 1.1 license are the same as above. |

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
| [`images/star-orbit/`](images/star-orbit/) | Source PNGs, 240 × 320 board preview, 72 × 72 guardian preview, 14 × 14 marble previews | Original visual assets for Star Orbit. Generated for this project with the built-in image generation tool on 2026-10-11; no third-party game images were used. `tools/generate_star_orbit_art.py` deterministically scales the sources and bakes the collision path into `main/art/*.rgb565*`, which ESP-IDF embeds in Flash. `gameplay-preview.png` is a composited illustration, not a device capture. The assets are distributed under the repository's MIT license. |
| [`images/eight-realms/`](images/eight-realms/) | 1448 × 1086 background atlas, 1774 × 887 transparent sprite atlas, eight 240 × 320 backgrounds, eight small sprites, 960 × 640 overview | Original Eight Realms environment and character art generated with the built-in image generation tool on 2026-10-13. `tools/generate_arcade_art.py` crops, downsamples, and converts them to RGB565/RGB565A8 under `main/art/eight-realms/` for Flash embedding. No third-party game art is used; the generated assets are distributed under the repository's MIT license. |

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

| File | Format | Use and source |
| --- | --- | --- |
| [`music/star-orbit-preview.wav`](music/star-orbit-preview.wav) | 16 kHz, 16-bit mono PCM, 16 seconds | Host preview of the original runtime synthesizer in `main/zuma_synth.c`, rendered with `tools/render_zuma_audio.c` on 2026-10-11. The firmware generates sound live; this WAV is not embedded. Distributed under the repository's MIT license. |

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.
