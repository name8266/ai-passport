<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Coin Pusher Arcade (Passport)

Experimental standalone arcade firmware on branch `feature/coin-pusher-arcade`.
It boots directly to a new coin-pusher cabinet rather than the hardware demo menu.

## Core simulation

- Up to 192 independent finite cylinders with position, velocity, radius, thickness and mass. Fixed-step (60 Hz physics / 30 Hz intended rendering), deterministic seeded xorshift, grid broad-phase and 2 contact solver passes, vertical gravity and stacking (floor-on-floor contacts favor pushing, elevated coins can settle on others), side-wall constraints, motor-driven reciprocating pusher, front ledge and side-loss pockets.
- This is **2.5D**, not a full 6-DOF rigid-body solver: cylinders stay upright, do not spin or tip, and contacts are approximations. Realistic density/throughput and target frame rate require physical-device measurement.
- Collecting at the center front awards virtual bank and score; dropping into the two side gutters loses coins. Copper 1, gold 3, ruby 15, giant 9 credits before combo/multipliers. Consecutive payouts within 2.4 seconds increase the combo up to 5. Each inserted coin grows the jackpot pool.
- Random effects: coin rain, magnetic attraction, earthquake, gold-rush x3, moon gravity, wormhole relocation, black-hole suction and jackpot pool payout. These alter the actual simulation state, not only the HUD.
- Three buttons: UP/DOWN move the drop cursor, OK click inserts a coin, OK double spends six virtual credits to force a random event, OK long pauses/resumes. Double-click UP or DOWN for a hydraulic pressure boost; at a full tray, OK also attempts a boost without charging credits. Double OK when out of coins gives 30 free-play credits.
- Fully offline. Saves a versioned full tray snapshot and virtual balance in NVS when dirty (five-second check, with a thirty-second idle snapshot; older 72-coin saves are migrated). No real-money play, payout, transactions or online component.

## Technical constraints / validation

ESP32-C3, ST7789 240x320, 8 MB Flash, **no PSRAM**. Uses a single LVGL draw callback for coins (no per-coin LVGL widgets and no full-screen canvas buffer). CJK title/event labels are enabled through the built-in Source Han Sans SC 16 subset; if a glyph is missing, the UI uses a safe English fallback. Check all on-device glyphs.

Host test:
```sh
cc -std=c11 -Wall -Wextra -Werror -Imain tests/test_pusher_physics.c main/pusher_physics.c -lm -o /tmp/test_pusher
/tmp/test_pusher
```

Full project gates: `./tools/validate.sh --static`, `./tools/validate.sh --firmware`.

### Unverified / next iteration

- Full hardware compile, USB flashing, true sustained FPS, worst-case internal RAM and LVGL object churn.
- Collision behavior for dense stacks, fairness/performance over long sessions, NVS power-cut durability.
- Auto anti-jam: when 142+ coins have been in play and no coin has reached an outlet for 9 seconds, a 3.2-second motor overdrive is attempted with 7-second cooldown. Coins are not erased or teleported to clear the tray. Full-tray insert attempts do not charge credits.
- More sophisticated 3D rigid bodies (tilt/spin/torque), sound and effects artwork are not implemented.
