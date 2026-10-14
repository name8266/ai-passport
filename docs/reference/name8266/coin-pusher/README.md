<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Coin Pusher Arcade

Standalone offline firmware on `feature/coin-pusher-arcade`, ESP32-C3 / 8 MB Flash / no PSRAM. Boots directly into a newly designed 240×320 cabinet.

## Play

The upper 32-pin board and lower coin tray share one screen. Steel balls have gravity, wall, pin and equal-mass ball contacts. Five physical exit bands produce copper, gold or ruby coins; outer bands enable hydraulic boost, gold bands upgrade the coin, and the center band triggers a random event when none is active. Coins enter the motor-driven tray at the actual ball exit position. Finite cylinder contacts support pressure transmission, approximate stacking, front payout and side loss. The solver is 2.5D: upright cylinders do not tilt or spin.

- Fixed capacity: 192 tray coins and 16 concurrent balls, with each flying ball reserving one tray slot. Rejected inserts spend no credits. Two grid contact passes run at nominal 60 Hz; rendering targets 30 Hz.
- Nine random effects: coin rain, magnet, earthquake, gold rush ×3, moon gravity, portal, black hole, rapid fire, jackpot. Rapid fire launches free gold balls every 160 ms when capacity and nozzle clearance allow. Full rain rewards grow the jackpot pool instead of disappearing.
- Copper/gold/ruby/giant values: 1/3/15/9 virtual credits. Consecutive front payouts within 2.4 seconds raise a combo up to ×5. Each paid insert adds one credit to the jackpot pool. A jackpot pays the pool and resets it to 30. These are offline game credits.
- UP/DOWN: aim left/right. OK click: launch. OK double: spend 6 credits for an event; with zero balance, receive 30 free credits. UP or DOWN double: pressure boost. OK long: pause/resume. At full capacity, OK attempts a free boost. Automatic boost after 9 seconds without progress at 142+ tray coins uses a 7-second cooldown.

## Rendering and persistence

AI-generated cabinet, coins, steel ball, peg, hydraulic bar and reward flash are integrated into the firmware. The conversion script `tools/convert_coin_assets.py` uses Pillow to create uncompressed RGB565 and RGB565+A8 C arrays. Image bytes remain in Flash; no PNG decoder, full-screen framebuffer, or per-coin widgets are allocated. Assets total 161,520 bytes; the BSP keeps its existing single DMA tile buffer. This reduces geometric drawing work but does not establish actual FPS.

NVS format 3 stores a checksummed snapshot under 4 KB, including the tray, in-flight balls, credits, pool, event and statistics. Position/velocity precision is 1/16 virtual unit. Formats 1 (72 coins) and 2 (192 coins) migrate on load. A worker checks dirty state every 5 seconds and otherwise checkpoints every 30 seconds. Sudden loss can roll back to the last committed snapshot; immediate power-cut preservation is not promised. No Wi-Fi or audio tasks are started.

Serial diagnostics every 10 seconds report simulation callback rate, worst physics time, free/minimum heap and largest internal block. `SIM Hz` is **not display FPS**; use a camera or completed display-flush instrumentation for actual displayed frame rate.

## Validation and delivery

Run `./tools/validate.sh` in ESP-IDF 5.5.3. The host physics test exercises all slot bands, deterministic runs, payouts, no-charge capacity failure, hydraulic cooldown, rapid fire and compact-save roundtrip/corruption. Twelve seeds each run 120 simulated seconds with frequent injections and events. Host sanitizers detect invalid accesses/undefined behavior but do not validate board speed.

Flash the verified merged image at `0x0` for a complete refresh; its padded gaps may reset NVS. The supplied segmented images with the unchanged partition layout are appropriate when existing NVS should be retained. Never flash the application-only image at `0x0`; its offset is `0x10000`. Do not routinely erase the whole chip.

Device tests remain pending: startup, button response, Chinese glyphs, dense-pile visual behavior, sustained displayed FPS, runtime minimum heap, pauses, and abrupt power loss/restart. A software layout preview is not a device screenshot. Sound, coin tilt/rotation and a full six-degree-of-freedom solver are outside this implementation.
