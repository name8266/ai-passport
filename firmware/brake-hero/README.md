<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Brake Hero verified firmware

Download `Brake-Hero-full.bin` for the FoloToy AI Passport (ESP32-C3, 8 MB Flash, 240 × 320 display). This merged full image is flashed at **0x0**. A merged flash can reset NVS records; confirm the intended device and data impact before flashing. Do not perform a routine full-chip erase.

- Build: PASS (ESP-IDF 5.5.3, merged-image/layout and matching ELF verified)
- Host tests: PASS (including 10,000 trajectories, audio worker and actual LVGL decoder)
- Device tests: NOT RUN
- Unverified: device 30 FPS, physical buttons/audio, minimum runtime heap, NVS power-loss recovery
- Repository static/complete gate: FAIL due to an inherited WorldCam README language-pair error

Size: 1,392,080 bytes. SHA256:

`6128ab20f18f0db50d13d2aab459975abfb85579771b33bf0c175f5b4f939940`

This artifact was built from the local uncommitted `feature/brake-hero` workspace. This dedicated branch uploads only the requested firmware, checksum and usage notes; it does not publish or replace the game source or modify `main`. The baseline code elsewhere on this artifact branch is not the source of this new game build. It is a fictional game, not a real-vehicle prediction.
