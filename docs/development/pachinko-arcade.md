<p align="right"><a href="pachinko-arcade.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Pachinko Arcade: Reward and Experience Design (Iteration)

> Status: Source changes are on `feature/pachinko-arcade`. No build or device verification has been started for this iteration.
> Offline entertainment only: all points, progressive pools, and medals are local virtual values. No deposits, money, cash-out, or redemption.

## Game loop

1. UP/DOWN adjust launch angle, OK shoots, double-OK nudges using charge, and long-OK cycles three modes.
2. Balls hit 33 stationary pegs and a central bumper. Five scoring pockets award 10/40/160/40/10 points; extreme pockets score zero in SURVIVAL.
3. Every manual launch contributes 12 points to the progressive pool. Every center pocket contributes an additional 30 and triggers a three-reel, five-symbol result.
4. Reels stop sequentially over roughly 720 ms. Prizes display prominent celebrations; some grant extra balls or fever time.
5. Predictable mission milestones grant scores and lasting medals. Lifetime medal count drives cosmetic cabinet rank.

## Progressive prizes and odds

The virtual JP pool starts at **500 points**, capped at **99,999**. It is independent of the current-round score. During ordinary rolls, each of the three reels independently selects one of five symbols (1, 2, 3, 5, 7).

| Symbols / condition | Score payout | Effect |
| --- | --- | --- |
| Exactly two matching | +45 | PAIR WIN banner |
| 111 or 222 | +120 | MINI WIN, +1 medal |
| 333 | +300 plus 10% of the pool above its 500 reserve | BIG BONUS, +1 finite-mode ball, +2 medals |
| 555 | +750 plus 25% of the pool above its 500 reserve | SUPER, +1 finite-mode ball, 10s fever, +5 medals |
| 777 | Entire current virtual pool; pool resets to 500 | MEGA, two instant free multiballs, 10s fever, +12 medals |
| All different | No reel bonus | Center pocket base score still applies |

Without guaranteed awards, a given triple (333, 555, or 777) occurs in 1 of 125 center rolls (0.8%), and exactly two matching symbols occur in 48% of center rolls. These are conditional reel probabilities, not a whole-game rate of return.

**Visible guarantee:** After 11 center rolls with no BIG or better, the 12th center roll is explicitly forced to 333 (BIG). `LUCK n/12` communicates the progress. Therefore, realized frequencies differ from unmodified independent rolls.

## Skill rewards and progression

- Missions cycle PE​​G hits → CENTER entries → BUMPER hits. First targets are 12, 2, and 2.
- Mission clearance awards `250 + 100 × stage` points (max 1,800) and one medal; every third clearance also grants two balls in limited-ball modes.
- Peg contact builds a combo up to ×8. The center bumper pays 25×combo.
- Bumper charge, SUPER, and MEGA can start 10 seconds of FEVER (pocket score ×3).
- Persistent cabinet rank: R1 (0–9 medals), R2 (10–39), R3 (40–99), R4 (100+). Medals are not redeemable.

## Modes

| Mode | Rules | Objective |
| --- | --- | --- |
| CLASSIC | 30 initial shots | High-score mastery |
| RUSH | 60 seconds, unlimited manual shots | Timed scoring and multiball rhythm |
| SURVIVAL | 12 initial shots, moving bumper, zero-value outer pockets | Precision and risk management |

## Technical and storage contract

- ESP32-C3, 240×320 RGB565, 8 MB Flash, no PSRAM. Target: 25 logic frames/sec via a 40 ms timer and two 20 ms physics substeps per frame. Actual LCD FPS and resource stability are unverified.
- UI objects and six ball sprites are allocated once; existing objects are moved or recolored each frame. No network, audio, or NVS work blocks the rendering loop.
- Round score, stage progress, and reel guarantee reset between runs. Best scores, pool, medals, total MEGA jackpots, and lifetime points persist to NVS.
- A background queue writes changed metadata approximately once per second; power loss can lose changes since the last successful NVS commit. Flashing compatibility and real-device persistence still require validation.
- Branch CI is manual-only (`workflow_dispatch`), not auto-triggered on each push.

## Future polish

Optional lightweight SFX/music, prize history, cabinet skins, and special ball tracks can follow after measured 20–30 FPS performance and internal RAM headroom are confirmed on-device.
