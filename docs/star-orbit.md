<p align="right"><strong>English</strong> · <a href="star-orbit.zh_CN.md">简体中文</a></p>

# Star Orbit

Star Orbit is an original, offline marble chase for the 240 × 320 FoloToy AI Passport. A weathered observatory, carved stone track, jade guardian, and four engraved gem marbles form its mysterious visual style. The art was generated specifically for this project and converted to Flash-resident RGB565/RGB565A8 assets; the exact gameplay track is baked into the board image. The game boots directly to its own title screen.

![Board artwork at native 240 × 320 resolution](../assets/images/star-orbit/board-preview.png)

The image above shows the board artwork without the live UI or marbles; it is not a device photograph. The build uses an opaque 240 × 320 board and small alpha sprites; it does not allocate a full-screen image in RAM. Regenerate the binary assets after editing the game route or source art with `python3 tools/generate_star_orbit_art.py` (requires Pillow).

## Controls

| Screen | UP / DOWN | OK | OK twice | Hold OK |
| --- | --- | --- | --- | --- |
| Title | Select Calm / Classic / Tempest | Start | — | — |
| Game | Aim; hold to rotate | Fire | Swap current and next marble | Pause |
| Paused | — | Resume | — | Return to title |
| Win / loss | — | Retry at same difficulty | — | Return to title |

Three or more adjacent marbles of one color disappear. The gap remains until the rear segment catches the front; matching colors on contact trigger a chain reaction. A match also pulls the front backward briefly. Further matches add a combo multiplier. Clear the finite wave before its front reaches the eclipse gate.

The difficulty levels vary both chain speed and wave length. The score and wave counter appear at the bottom; the battery indicator is at the top right. A separate task synthesizes an original 16 kHz pentatonic theme with a low drone, stone-drum pulse, and distinct event cues. [Listen to the 16-second host preview](../assets/music/star-orbit-preview.wav). If the codec is unavailable, play continues silently.

The title, difficulty selector, controls, pause, win, and loss screens now use Simplified Chinese. The fixed UI uses 14, 16, and 20 px Source Han Sans SC subsets; `python3 tools/generate_star_orbit_font.py --check` verifies every UI character. During play, LVGL redraws the changed marble bounds, launcher, and flying marble instead of invalidating the entire screen every game tick. The game targets a 33 ms update period and logs game-loop rate, LVGL render rate, and flushed pixel count in five-second windows. A stable 30 fps claim still requires a measured on-device session while playing; the LVGL counter is not a physical screen measurement.

## Validation checkpoint

- Source branch: `feature/zuma-three-key`, based on official `FoloToy/ai-passport` `main` at `a06235e`.
- Host logic: `tests/test_zuma_game.c` covers path monotonicity, difficulty, controls, matching, pause, win, loss, and 25 seeded long rounds with state invariants.
- Art budget: the embedded board and sprites total 171,504 bytes in Flash. Compared with the pre-art build, the linker reports the same 110,964 bytes of static internal-RAM use; 210,332 bytes remain before runtime allocations. This is a build-time budget, not a measured free-heap value on the device.
- Hardware acceptance still requires a device run: check the rendered art and 30 fps pacing, button direction and hold timing, audio without gaps while firing, battery readout, and successful win/loss transitions.

## Device checkpoint (2026-10-11)

The user authorized flashing the verified bundle identified by SHA-256 `cd5e03a78cc54d6097f148a16187815240f6d97e48a0bb376666727762f2565e` to `/dev/cu.usbmodem101`. A read-only partition-table check found an existing 4 MB `archive` data partition at `0x400000`; the new default partition table would claim that space for the factory app. To preserve the existing table, NVS, PHY, and archive data, only the verified application component (`0f3c32c6051aba7892b1dc4bd7cee851f516ec734329d689b26a1528a6ece5e1`) was written at `0x10000`. The app fits the existing 4032 KB factory partition. Esptool verified the written data hash.

A bounded serial monitor captured a clean boot through display (240 × 320), LVGL, buttons, battery gauge, ES8311 codec, and 16 kHz mono output initialization. No errors appeared during the following 10 seconds. Fresh host simulation and AddressSanitizer/UndefinedBehaviorSanitizer runs passed for the game state machine and audio synthesizer. Visible frame pacing, on-device artwork, loudspeaker output, button response, and win/loss gameplay still need user observation.

The checkpoint above applies only to the identified older bundle; the Chinese UI build has a separate device checkpoint below.

## Chinese UI and 30 fps checkpoint

The complete static and firmware gates pass on ESP-IDF 5.5.3. The current verified full-image SHA-256 is `35419fd7f59a4fb15d614ca87298108920189f4ea9989f8e9e77dcbb6a0577a4`; its app image SHA-256 is `1de0e5198136d30701392c0b61d06da46c1f68549d9d5591ba1cd0278ec0833e`. The app is 900,640 bytes and fits the device's 4032 KB factory partition. The 143-character fixed UI/ASCII inventory is present in all three compiled font sizes.

The user authorized flashing this build to `/dev/cu.usbmodem101`. Read-only checks confirmed the same ESP32-C3 and existing partition table. Only the application image was written at `0x10000`; the partition table, NVS, PHY, and archive were preserved. Esptool verified the written data hash. A serial monitor captured a clean boot of the matching ELF (`f74ce16dd6fce4c77e1b711ba08950ec1044fc75f00bb6dddd79c21e45dc02de`) through display, LVGL, buttons, battery gauge, ES8311 codec, and 16 kHz mono output initialization. A play-session frame-rate measurement and visual/audio acceptance remain pending.
