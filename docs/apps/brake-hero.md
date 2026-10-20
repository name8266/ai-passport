<p align="right"><a href="brake-hero.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Brake Hero 2.1 engineering notes

## Source and scope

The previous GitHub artifact branch contained no game source. This repair reconstructs the complete recorded source changes from the original development chat on baseline `869d212ac12d6b994604581000bdc3bfdfa39465`. Display-sized PNGs, bitmap glyphs and PCM8 audio were recovered from the exact previously flashed full image, SHA256 `6128ab20f18f0db50d13d2aab459975abfb85579771b33bf0c175f5b4f939940`. All compiler inputs for this repair are retained locally. Original large AI-generated atlases and higher-resolution synthesis inputs were unavailable; the recovered resources are documented honestly in the [asset inventory](../../assets/README.md).

## Third-person vehicle

`brake_vehicle.c` now composites a retained detailed rear-body sprite and an animated textured canopy in the same centered coordinate system as the road. The old car used a yawed camera basis while the road remained centered, which made the car appear pointed across the lane. The new view has zero yaw/roll and a silhouette-centering regression check. The V800-inspired body was generated with built-in image_gen using the official exterior as a reference. Source RGBA and exact prompt are retained in `assets/images/brake-hero/source/vehicle-rear-v21.*` (prompt in `vehicle-rear-v21-prompt.txt`). The runtime car is 136 x 123 indexed pixels; glass/paint/chrome detail shares a weighted 223-color texture palette with the scenery. No per-frame allocation or full-screen decoder is introduced. It is game art, not manufacturer CAD or real crash data.

`bh_vehicle_pose` derives the roof lift from the same continuous 0–100% tent value used by physics and the Chinese HUD. A front-hinged hard shell and both cloth sides move together, including passive retraction. Functional pedal force lights both rear lamps and the upper stop lamp. Broken brakes extinguish these game lamps. Collision animation uses game impact speed and elapsed time for front crush, glass cracks, fragments and dust. A cliff uses a pitched falling trajectory. The crash scene remains visible for 2.6 s before results. These are game visuals, not physical vehicle damage validation.

## Display and audio scheduling

Device evidence for the old image showed CPU0 IDLE watchdog timeouts while `taskLVGL` and `brake_render` were running, with 8.79 FPS and a maximum 122234 us frame interval. That establishes starvation/performance trouble, not a fully proven audio root cause.

The new runtime uses the BSP's existing ST7789 panel directly, without starting LVGL. A single renderer owns the 77824-byte indexed framebuffer and a 19200-byte (240 × 40 × 2) DMA stripe. A 512-byte RGB565 wire palette converts each stripe. The completion ISR releases a semaphore; the stripe cannot be overwritten before completion. Each transfer has a 150 ms timeout; on failure the renderer stops without freeing a possibly active DMA buffer. Input and audio tasks never wait on a full-screen LVGL lock. Existing rounded-screen masking is reproduced in the final indexed frame.

The audio worker owns the codec at priority 5, above render priority 2 and below the short logic task at priority 6. It preserves urgent-event preemption, bounded queues, round cancellation and 20 ms PCM chunks. It relies on blocking I2S writes rather than an extra delay between chunks. Logs expose the maximum active-clip feed interval so the next device test can compare it with the BSP's at-most-90 ms I2S DMA capacity. Intentional silence between announcements is not counted as an underrun. This addresses the observed scheduling problem; physical audible continuity is still unverified.

Physics remains 60 Hz with timestamped input, independent of rendering. The three controls decay separately. The exact 1600 N boundary, permanent round-level brake failure, 500 m track, obstacle selection, top-ten format and CRC-protected score storage are preserved. Storage commits happen in a separate low-priority worker only after a qualifying result.

## Font and regression checks

The 14 px Noto SC bitmap font includes all fixed source strings plus printable ASCII. The converter rejects missing source-font glyphs and records `assets/fonts/brake-hero/inventory.json`. A known-absent U+9F98 negative case remains tested. Driving labels use Chinese, including throttle, tent opening and brake force. Final appearance is checked in the shared native renderer; a successful font build alone does not prove device rendering.

`tools/test_brake_hero.sh` covers timestamped physics, score CRC/restart behavior, navigation, audio urgent preemption and round cancellation, exact continuous PCM chunk coverage including a short final chunk, tent opening/retraction, brake lamp states, crash/cliff poses, DMA row boundaries and wire color order. It generates 10000 deterministic trajectories; [statistics](brake-hero-simulation.json) describe the fictional game, not a real car. Address/undefined-behavior sanitizers additionally cover the shared vehicle renderer and physics.

Use `./tools/validate.sh --firmware` in ESP-IDF 5.5.3 to create and archive the verified full image. Verify its content-addressed archive using `python3 tools/archive_firmware.py verify <archive>`. The full repository gate still encounters an inherited WorldCam README language-pair error, reported separately without suppressing the check. Host fault-injection checks do not establish device timing.

## Device acceptance pending

New-image device tests are NOT RUN until explicitly authorized. Check all screens and Chinese text; tent opening/closing versus percentage; lamps with braking and broken brakes; all four impact animations; intelligible audio during sustained driving; input release; completed FPS, feed gaps, minimum heap and largest block; absence of watchdog errors; score persistence after restart. Flash only the exact verified full image at 0x0 after consent for its target and possible NVS reset. No whole-chip erase is required. Old-image watchdog logs must only be decoded with the old matching ELF; a new ELF is not a substitute.
