<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Brake Hero 2.0 verified firmware

Download [Brake-Hero-v2-full.bin](Brake-Hero-v2-full.bin) for FoloToy AI Passport (ESP32-C3, 8 MB Flash, 240 × 320 display). This is the merged image for **0x0**. A merged flash can reset NVS settings and scores; confirm the exact device and data impact first. No routine whole-chip erase is required.

The third-person V800-inspired low-polygon vehicle has continuously animated tent opening/closing, functional brake lamps, impact deformation, cracked glass, debris and dust. Driving labels are Chinese. Direct panel DMA and a higher-priority audio worker replace the old full-screen LVGL rendering path. The mesh and damage are original game effects, not manufacturer CAD or real crash predictions.

- Version: **2.0.0**.
- Build: **PASS**, ESP-IDF 5.5.3, image/layout and matching ELF archive verified.
- Host tests: **PASS**, including 10000 driving trajectories, continuous PCM sample coverage, lamp/tent/damage states, DMA bounds/color order, sanitizer checks and baseline BSP/workflow tests.
- Generated assets: **PASS**, 22 linked byte arrays and 271 glyphs compared against the retained matching ELF.
- Complete repository gate: **FAIL** on inherited WorldCam README language-pair errors; not suppressed.
- Device tests: **NOT RUN** for this new image. Actual FPS, audible continuity, screen appearance, controls, watchdog stability and NVS persistence remain unverified. Old-image device failures are not counted as acceptance of v2.

Size: **949648 bytes**. SHA256:

`6814d7f5e7faad12976ed451921c910ba50a43dde899b343c518cf59bdcc9629`

Matching ELF SHA256: `6fb2c63832aa92c46e8319d01511cb17ca1f88b28a5c6a74df30f50840fd0bd4`. ELF/MAP remain local and are not published in this artifact folder.

Unlike the old binary-only branch, the complete repaired source is available at [feature/brake-hero-v2](https://github.com/name8266/ai-passport/tree/00cb40628c79ea74f9c9161e2dec46e858005dd9). The [source/build manifest](Brake-Hero-v2-manifest.json) records the exact compiler inputs. Repository root files on this artifact branch contain that source too. Build directly from the included generated C assets; no image or speech service is needed.

![Shared native renderer preview, not a device photograph](Brake-Hero-v2-preview.png)
