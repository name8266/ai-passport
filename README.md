<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Brake Hero 2.1

Offline braking game for FoloToy AI Passport: ESP32-C3, 8 MB Flash, 240 × 320 display, no PSRAM.

The car and straight road now use the same centered rear chase view, with zero yaw and roll. A detailed generated V800 rear-body sprite replaces the coarse polygon body; glass, pearl paint, chrome, vertical tail lamps and panel contours remain visible at the native display resolution. Roof lift, cloth, braking lamps, impact deformation, cracked glass, debris and dust remain live game effects. The image is retained with its exact generation prompt; it is not manufacturer CAD or a crash prediction.

## Controls

| Physical button | While driving |
| --- | --- |
| UP | Open the roof tent |
| DOWN | Increase brake pedal force |
| OK | Increase throttle |

Hold to increase; release to let each control decay independently. The board has a single ADC ladder and does not support combined keys. All driving indicators and warnings use Chinese, including tent opening, braking force, throttle, remaining distance and brake failure.

Accelerate on the finite 500 m track, then stop before the obstacle. Exceeding the fictional 1600 N pedal threshold permanently breaks the brake for that round. The tent remains available for rescue. Peak speed must reach 100 km/h to qualify for the local top ten. Scores use versioned CRC-protected NVS records.

## Build and checks

Use ESP-IDF 5.5.3 and the locked managed dependencies:

```bash
source /path/to/esp-idf-v5.5.3/export.sh
./tools/test_brake_hero.sh
./tools/validate.sh --firmware
./tools/validate.sh
```

The complete gate currently encounters the inherited `worldcam-site/README.md` language-pair error; it is not suppressed. The independent firmware gate builds from tracked defaults, merges the full image and retains its matching ELF/MAP archive. Download only the merged full image and flash at **0x0** after confirming the target and data impact; it can reset existing NVS settings. Do not routinely erase the whole chip.

## Audio and rendering

One priority-5 audio worker owns ES8311 and streams 20 ms PCM blocks. The priority-2 renderer uses one indexed frame and one 40-line DMA buffer, waiting for SPI completion before buffer reuse. It does not run full-screen LVGL image decoding or hold an LVGL lock. Logic remains timestamp-driven at 60 Hz; the frame deadline does not control physics. Diagnostics report completed frames, heap, dropped inputs and the maximum active-clip PCM feed gap. Hardware FPS and audible continuity require device acceptance.

See [engineering notes](docs/apps/brake-hero.md), [simulation results](docs/apps/brake-hero-simulation.json), and [asset provenance](assets/README.md).
