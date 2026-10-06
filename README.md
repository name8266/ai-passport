<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Neon Arcade · Classic Game Collection

A Chinese-language offline collection with classic slots, multiline slots, roulette, overhead dice and Plinko.

## Game modes

| Mode | How it works |
| --- | --- |
| Classic three-reel slots | Continuous reels; middle-line pairs award 5 points. Triples of cherry/lemon/BAR/bell/plum/red 7 award 20/25/35/45/60/100 points |
| Five-column multiline slots | A 5×3 grid with 27 triple patterns: 9 horizontal, 6 diagonal and 12 triangles. Horizontal patterns use base points; diagonals/triangles use twice the base. Without a triple win, matching leftmost pairs award 5 points per row |
| Roulette | A traditional 37-pocket wheel numbered 0–36; read the settled ball's number and color |
| Overhead dice | Choose 1–6 six-faced dice and read individual faces and the total |
| Plinko | Release 1–10 balls together into nine bins; the round ends after every ball lands |

Slots show round points and separate cumulative points for each mode. Points are for entertainment, without deposits, wagering or redemption. Plinko simulates independent trajectories, without ball-to-ball collisions.

## Three-button controls

- Home: Up/Down selects a game; Confirm enters; hold Confirm for settings.
- Game: Confirm starts; hold Confirm returns home; Down toggles sound; hold Up toggles autoplay.
- While idle: Up changes slot/roulette speed, dice count or Plinko count.
- Changing Plinko count stops autoplay; hold Up again to resume.

## Settings

Hold Confirm on home → Up/Down selects a row → Confirm changes the value → hold Confirm returns.

| Setting | Choices | Default |
| --- | --- | --- |
| FPS display | On / off | Off |
| Dice count | 1–6 | 2 |
| Neon effect | Rainbow flow / rainbow breathe / dual chase | Flow |
| Sound | On / off | On |
| Speed | Slow / normal / fast | Normal |
| Slot mode | Classic three-reel / five-column multiline | Classic |
| Plinko count | 1–10 | 1 |

To select multiline slots: Settings → Slot mode → Five-column multiline → home → Slots. Change dice/Plinko speed in settings. Settings and points are RAM values and reset on restart. No networking or account is needed. Startup backlight command is 100%.

## Download and flash

Download `FoloToy-AI-Passport-full.bin` from the latest Release and flash the merged image at `0x0`. A full flash may reset stored data. Never flash an application-only image at `0x0`.

The verified image already flashed and observed booting has SHA256:
`1ef0f63ab6e62da6454334d4dd41963971b929e65187bd2195d6efeaf89df1b0`.
It was built from the pre-release workspace and reports `06ad991-dirty`; the release commit preserves the corresponding source.

## Build and verification

ESP-IDF 5.5.3; run `./tools/validate.sh`; target ESP32-C3 with 8 MB Flash.
Build: PASS. Host tests: PASS, including all 27 multiline paths and all 1–10 ball counts at three speeds. Device: written hash, startup, 127-glyph coverage and home text audit passed. Full visual/gameplay acceptance of multiline slots and simultaneous balls is reserved for the user's manual test; no 350-round regression is claimed for this build. The earlier 317-round regression applies to the older six-fix firmware.

The cover is an AI-created game collage. Supporting images use project assets, rendering and simulated trajectories. Every image is a simulation, not a device photograph. See the assets documentation for font and artwork provenance.

[Latest release](https://github.com/name8266/ai-passport/releases/tag/v2026.10.07-neon-arcade)

![Simulated game collage](https://github.com/name8266/ai-passport/releases/download/v2026.10.07-neon-arcade/neon-arcade-cover.png)
