> **2026-10-17 revision:** 180 maximum physical bodies, 156 at start; the 256-body V3 description below is historical. See [180-body review](./180实体优化验证.md). Host/firmware CI and device acceptance are tracked separately.

<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# 3D Coin Pusher — Dense Cabinet Candidate

Candidate version: `coin3d-256-v3-20261016`. Device acceptance is pending: the
USB serial device is currently unavailable. **20 FPS with the dense field has
not been measured.** The last device-accepted image remains the V2 build
identified below. Do not advertise this candidate as a verified 20 FPS release.

## Changes in V3

- Long-press OK → cabinet menu → **Sound: on / muted**. Toggle with OK;
  long-press OK returns to play. The separate `cp_rogue/muted` NVS key survives
  reboot and fresh runs without changing the leaderboard format. Missing or
  invalid settings default to sound enabled. Saving and codec volume changes
  run in the FX worker. Muting retains jackpot screen/backlight animations.
- Start with **256 physical bodies (253 gold coins and three prizes)**. The
  pool holds 288 bodies, leaving 32 positions for inlet coins, refills and
  events. Staggered lower-bed layers expose the denser field; placement uses
  actual convex overlap checks. Existing mechanical fixtures retain their
  original cabinet. This is real 3D contact, support and rotation.
- Contacts occupy **40 bytes instead of 112**, with capacity for 2,048
  contacts. The tangent frame is reconstructed exactly, other-body arms are
  derived from the contact point, and inverse mass/Jacobians are recomputed
  from the unchanged rigid pose. Spare contact storage caches exact Jacobians
  during repeated solver passes, prioritizing body pairs.
- Remove duplicate float pose snapshots, shape constants and vertex arrays;
  prize polygons share the exclusive frame arena. Compact displayed poses and
  20-row tiles reduce renderer RAM. Physics and rendering retain one owner.
- Use indices wide enough for bodies 255–287 and distinct pair/warm-cache keys.
  Circulation thresholds scale to 264/224 bodies. The rear hopper targets 200
  bodies, checking an empty physical volume at most once per 1.2 seconds.
- Serial diagnostics add `muted` and `DENSE_FRAME` measurements for frames with
  at least 250 bodies. An average after the field empties is insufficient to
  establish dense-field performance.

## Candidate validation

The original mechanical assertions, all-relic stress, compact/full renderer,
NVS setting tests, high body-index checks, and 10,000 exact tangent-frame
reconstructions are covered by the complete gate. A 30-run mixed-policy sample
ended naturally with 14 victories, median 745.14 simulated seconds, peak 1,753
contacts, zero contact overflow and zero capacity waits. Six runs under
ASan/UBSan and physics assertions matched the corresponding ordinary simulation
outputs exactly. These are host results, not MCU frame-rate evidence.

Pending on device: sustained 250+ body frame rate, minimum free heap during
NVS/audio activity, mute persistence across a real reboot, audible silence and
sound restoration, Chinese menu pixels, and muted jackpot backlight pulses.
Community/GitHub release publication should follow that acceptance.

## Previous device-accepted V2 reference

The remaining sections record V2 behavior and its historical test evidence;
its 88-body/112-capacity numbers do not describe the V3 candidate.

# 3D Coin Pusher — Fate Expedition Update

Current version: `coin3d-royale-v2-20261016`. Offline play with a Chinese interface. Every boot starts a
fresh run; local rankings and relic discoveries persist. This update extends the run, bounds individual
relic power, loads a denser physical field and automatically queues special coins.

## Five acts and resource pressure

Start with **40 normal drops**, **88 physical bodies**, two six-layer towers and three colored prizes.
Queued special coins are additional free drops; normal credits are spent only when that queue is empty.

| Act | Chips earned this act | Bodies collected this act | Time limit |
| --- | --- | --- | --- |
| 1 | 450 | 55 | 180 seconds |
| 2 | 750 | 85 | 210 seconds |
| 3 | 1,100 | 110 | 240 seconds |
| 4 | 1,550 | 140 | 270 seconds |
| 5 | 2,200 | 170, plus the crown | 300 seconds |

Earnings and collection quotas restart each act; excess earnings cannot skip the next act. Shop spending
does not undo earned progress. After acts one through four, choose a contract: **Steady Supply** grants
12 drops and one Guard coin; **Build Supply** grants seven drops and two related special coins;
**High Pressure** grants 18 drops and two side towers, but raises the next chip goal by 25%.
The final act delivers a physical crown tower; a side-lost crown can be delivered again. Menus, queue
inspection and drafts pause the clock.

Normal gold is worth one chip; every 16 gold coins restores one drop. A blue chip is worth 12 chips and
one drop. Red/blue/green prizes pay 18/24/12 chips; every two colored prizes grant one drop and one queued
special coin. Fever pays approximately 1.5 times value with alternating integer rounding. When both credits
and special stock run out, a 20-second grace period permits payouts to revive the run; insurance still works.
The shop sells five drops for `32 + 8*previous purchases - 3*Vending level`, with at most `7 + Vending level`
purchases per run. Chips are virtual, with no cash value. Rankings use the settlement balance; older records
remain but scores under different rules are not equivalent.

## Automatic special-coin queue

The initial order is Seed, Water, Prism and Blast, followed by the first relic's related gift.
Later rewards enter a 32-entry FIFO, capped at four coins of each type. Excess rewards become bounded
normal bonus drops. Confirm, bursts and autoplay all consume the same queue head, without manual selection.
A busy inlet consumes neither stock nor credits. Autoplay still requires the player to choose relics and
contracts; the separate four-second special-cast timer has been removed.

Down opens a stock/effect inspector, not a reorder screen. The HUD shows the next coin and queue length.
The eight physical effects and contact combinations remain: Seed/Water growth, Magnet/Meteor bursts and
Prism-amplified Blast or Magnet. They require actual contact, so a cast does not guarantee a combination.

## A fuller cabinet and arcade presentation

Seismic events now shake coins touching the payout bed; upper layers receive motion through real contacts instead of simultaneous launch impulses.
The pool remains 112 bodies with no additional framebuffer. The starting field adds 36 physical bodies. Extra upper-deck coins occupy supported layers rather than hanging beyond the initial shelf edge. Low-energy sleep uses 4 mm/s, 0.3 rad/s and 0.35 seconds, with contact/support changes waking bodies; original mechanical tests remain unchanged.
Below 72 bodies, a rear hopper attempts one checked empty-volume shelf spawn every 0.4 seconds. Existing
bodies are never deleted or teleported. Capacity relief now starts at **92 bodies and stops at 80**, replacing
68/52; a moderately loaded field no longer triggers automatic emptying. Rain, prize restocks, side deliveries
and deferred spawns remain bounded. The inlet still relies on the moving rod and peg contacts for landing.

Reels now use an energy scan and a complete named reveal instead of three repeated characters. Event names
represent Overdrive, Golden Cascade, Lucky Guard, Royal Vault, Void Siphon, Stellar Replication, Twin Towers,
Seismic Surge and Aurora Fever. Recessed segmented windows sit inside metallic lane frames, with chasing
shelf lights and side lamps. Jackpots retain sound and physical backlight pulses. Existing cabinet/gold art
is retained; animated light is rendered natively.

## Base values for all 28 relics

These values exclude synergy extras. Each relic has three tiers. A synergy requires both relics and at least
four combined tiers. Interest, combos and fusion have finite payouts. Hourglass refreshes to the longer
remaining duration instead of accumulating repeated events indefinitely.

| Relic | Updated rule |
| --- | --- |
| Magnetic Pull | Limited central and forward force increases per tier |
| Lunar Gravity | Gravity reduced by 7% per tier |
| Frost Slide | Bed friction reduced by 12% per tier |
| Bounce | Restitution coefficient 20 + 18*tier; Moon adds 12 |
| Swift Rod | Rod speed increases by 25% per tier |
| Tidal Pusher | Physical stroke increases by 2 mm per tier |
| Coin Echo | Paid drops have a 7/14/21% chance of one bonus coin |
| Ripple Chain | Local payout impulse, with a 12-tick cooldown |
| Wormhole Return | Siphoned payouts have an 8/16/24% bonus-coin chance |
| Stellar Fusion | Every 16/14/12 gold pays 1/2/3 chips and one bonus coin |
| Space Fold | Side losses have a 10/20/30% bonus-coin chance |
| Alchemy | Normal gold has a 3/6/9% chance to queue a blue chip |
| Resonant Rails | Intermittent second lane / two lanes / three lanes |
| Lucky Dice | Reel jackpot chance 1/29, 1/26, 1/23 |
| Hourglass | Events last 12/24/36% longer; refresh to the longer remainder |
| Combo Rhythm | After combo four, every 8/7/6 eligible payouts adds one chip |
| Chip Interest | 1/2/3% of balance per reel, capped at 4/8/12 chips |
| Thrift Coupon | One drop refunded every 10/8/6 paid drops |
| Chip Exchange | One drop per 120/100/80 chips earned |
| Emergency Insurance | Each upgrade adds one five-drop rescue |
| Supply Pack | Two drops on selection; an extra drop every 5/4/3 prizes |
| Vending | Three-chip discount and one extra purchase allowance per tier |
| Treasure Map | One prize queued every 42/36/30 gold |
| Mirror Chest | Prizes have a 10/20/30% chance to repeat their effect |
| Thunder Ring | Five events release 2/3/4 seconds of thunder |
| Antigravity | Reels grant 1/1.5/2 seconds of hovering |
| Meteor Forge | Every 12/10/8 gold side losses queues a heavy blue chip |
| Rewrite Fate | 1/2/3 rerolls per draft; the first choice may favor owned relics |

## Controls and validation

- Confirm casts the queued special, or a paid normal coin when empty. Double-confirm queues two inputs.
- Up toggles autoplay.
- Down inspects the queue; hold Down for a three-input burst.
- Hold Confirm opens the pause menu. Confirm or hold Confirm closes the queue.
- Drafts/contracts use Up/Down and Confirm; holding Confirm in a draft spends an available reroll.
- Confirm after settlement starts over. Reboot starts fresh while retaining records/discoveries.

Thirty final mixed-policy host runs produced 6 wins, with a median simulated duration of 463.19 seconds;
winning runs lasted 627.00–913.53 seconds. This describes those automated policies, not a player win rate.
Magnet, Fusion, Combo, Interest and Map were also tested alone at tier three for four runs each, with no wins;
a four-run no-relic baseline also had no wins. No state error, contact overflow or capacity wait occurred.
Physics remains a discrete rigid-body approximation, without a zero-intersection or full rotating-convex-CCD
guarantee. Active-play performance targets 20–30 FPS. Build, host, device and unverified results are recorded
separately in this revision's delivery evidence.

### Final device handoff

Complete gate: PASS. Image `dd3986db1c8a63a15dfc0636825cd2931429bc0936633e3eb8c2c12dde96c8aa` was flashed with NVS preserved. Bounded normal/event practices measured 22.4–30.0 / 27.6–30.0 active FPS over ten-second windows; maximum intervals were 68/61 ms, so this is not an every-frame 50 ms guarantee. Final-image checks confirmed Chinese captures, free FIFO-head insertion and completed jackpot audio/backlight commands. A fresh 40-credit/88-body draft is ready. Physical feel, real sound/light appearance and full long-campaign experience remain for player acceptance. Evidence is in `build/royale-v2-final-dd3986db/`.

## Historical gameplay and acceptance

The following sections are retained as history. Their values, manual special controls and three-act campaign
do not apply to the Expedition revision described above.

## Tactical coins and three acts, 2026-10-16

Inspired by special-coin contact interactions and builds in the [official RACCOIN description](https://www.playstack.com/games/raccoin/),
this cabinet independently implements eight tactical coins. No RACCOIN code, artwork, audio or characters are copied.
The original materials, fixed inlet and physical pins remain.

The starter tray holds one blast, seed, water and prism. Each relic selection also grants its associated special,
identified below the draft. Colored prize collection supplies a random special; intermissions offer two of a chosen type
and eight insertion credits. Stock caps at four of each type; excess becomes ordinary inlet rewards, still subject to
the existing 4,096-coin queue limit. Casting does not spend insertion credits. A busy inlet preserves inventory and
shows a retry message. Specials use the same rod, pins and guide and can activate lit lanes; ordinary free coins cannot.

| Special | Physical action |
| --- | --- |
| Blast | One second after gaining support, applies forward/upward impulses within 32 mm; prism contact expands this to 48 mm |
| Magnetic core | Attracts bodies within 52 mm for six seconds after landing; prism contact also marks attracted ordinary bodies for double value |
| Seed | Waits for actual water contact, then grows six physical gold coins if the overhead volume is clear; grows once |
| Water | Eight seconds of activity after landing; flows toward an unactivated seed within 56 mm, but growth requires contact |
| Prism | Actual contacts over eight seconds mark ordinary gold for double base value; marks do not compound |
| Hammer | A physical blue chip with twice gold mass, worth 25 chips and one insertion; magnetic-core contact triggers one 42 mm burst |
| Escort | Gaining support activates side protection, modified by the hourglass |
| Echo | Gaining support queues three ordinary inlet rewards |

Special gold keeps its gold material with a small identifying stamp. Hammer reuses the existing blue-chip appearance.
A prism-marked seed grows double-value gold. Effects remain within the cabinet. Non-speculative SAT contacts record flags;
spawning and velocity changes happen only after the full solver tick. Growth waits for room in the physical pool.
Expired specials remain pushable, collectable bodies.

The acts require cumulative earnings of **300 / 1,100 / 4,000 chips**. Act two additionally requires five collected
colored prizes; act three requires collecting the physical crown riding a side-delivered tower. Shop spending does
not undo gross-earnings progress; ranking still uses the remaining wallet. Act time limits are **90 / 120 / 150 seconds**.
Menus, drafts and trays pause time. Intermissions advance the act; the third act queues two side deliveries.
A side-lost crown becomes eligible for another delivery after eight seconds. The final objective awards a jackpot
and ends the run. Time expiry, depleted credits or voluntary settlement also end it. The previous leaderboard schema
is retained, but earlier scores were earned under different rules.

Autoplay attempts an inventory special every four seconds, preparing seed/water and magnetic-core/hammer pairs, then rotating other stocked types to avoid starvation. Serial `autorun N` runs 1–30 complete practice rounds, choosing relics/intermissions,
using the shop at low balance and starting subsequent rounds. `autorun 0` stops the automatic controller; `reset`
also stops it. Practice never writes ranking/discovery. `cast 1` through `cast 8` consume real inventory without
granting cheats. Completion leaves the result screen; manual `reset` returns to a fresh run.

Draft/codex descriptions show base current/next-tier values. The HUD shows the act, gross target and time left.
The native 12/20 px font binding and source/cmap coverage checks include the new text.

## Design review against RACCOIN

The official reference emphasizes special-coin interactions, stacked item builds and distinct run strategies.
This revision gives those ideas three connected resources: insertion credits sustain the run, tactical stock
lets the player time a physical intervention, and chips fund a limited refill at the cost of the final ranking.
The three acts supply a finish line rather than an endless score counter. Intermission supplies and prize
restocking keep tactical choices available after the initial field has been pushed out.

A seed/water pair produces physical gold only after contact and available overhead clearance; prism/blast
and magnet/meteor pairs change the force applied to the actual pile. These interactions coexist with all
28 relics and the existing reel events. Their outcome still depends on where bodies land and move.
The stock tray and numeric draft descriptions expose what the player can control. This is an independent,
small-device interpretation; it does not contain RACCOIN's complete coin, character or item roster.

### Collision and drawing corrections in this revision

The earlier failed collision audit below is retained as history. The current solver normalizes near-parallel
edge axes before truncation, rejects non-unit or deep cached cap contacts, and normalizes near-center feeder
capsule contacts at higher precision. The original strict support and multilayer pushing assertions remain
unchanged. The axis-division acceleration is checked against one million exact integer quotients.

Drawing retains the photographed cabinet and gold assets. It reuses the physics contact scratch between
steps for palette indices, color tables and polygon caches, and transfers only dirty X/Y scanlines.
A factored coin-orientation lookup matches one million brute-force selections exactly. Full/dirty rendering,
including cropped DMA rows and frames spanning several physics steps, must remain pixel identical.
Wall-clock and active-play frame rates are logged separately; a 30 Hz physics clock alone is not a 30 FPS claim.

## Roguelike run, chips and records

A run starts with **30 paid insertions** and a three-choice starter relic. Collecting
bodies grants experience: gold 1, a physical blue chip 5, a colored prize 7.
The first upgrade requires 20 XP, then thresholds rise by 10, capped at 300.
Each upgrade pauses the cabinet for three randomized choices. Relics stack to
three levels; maxed relics leave the candidate pool. All 28 are available from
the start. Selecting one records its discovery locally, with no permanent power
bonus carried into another run. A seed and choices produce different builds.

Gold pays one chip; every ten collected gold coins refund one insertion.
Physical blue chips are twice the mass of gold and pay 25 chips plus one
insertion. Colored prizes pay 40/60/25 chips and one insertion. Fever doubles
physical collection payouts; combo and other relics can modify the economy. A
jackpot pays 50 chips and three insertions, alongside its physical rewards.
Chips are virtual game currency with no monetary value. The shop trades 20
chips for five insertions, at most three times per run before relic bonuses.
Spending chips reduces the final ranking score. Coin balance is capped at 999,
chip balance at 9,999,999, and all pending physical spawns remain bounded.

When insertion credits reach zero, existing physics/rewards continue for 20
seconds to allow refunds. Fresh mystery draws, automatic towers and prize
restocking require recent paid play and a positive credit balance. Already
owed deliveries still finish. Free reward coins do not trigger gate reels;
automatic paid insertion interleaves with rain rather than waiting for the
whole queue to empty. A successful refund resumes play. Insurance can rescue
a depleted run; otherwise it ends. Productive combinations may keep earning
additional insertions. The player may settle voluntarily at any time through
the pause menu. Pausing freezes physics and the grace countdown.

| Context | UP / DOWN | OK | Hold OK |
| --- | --- | --- | --- |
| Cabinet | Toggle auto / open tactical tray | One insertion | Pause menu |
| Tactical tray | Browse eight specials | Cast selected inventory coin | Return to cabinet |
| Intermission | Select one of three | Two specials and eight insertions | Same as OK |
| Draft | Select one of three | Gain/stack selected relic | Free reroll if available |
| Menu | Select continue, build, ranking, shop, settlement | Enter | Continue |
| Shop | Select buy/return | Buy five insertions or return | Return |
| Ranking | Select among ten records | Inspect that record's build | Return |
| Codex | Browse all 28 relics | Return | Return |
| End | Ranking / build | New 30-insertion run | New run |

Hold DOWN queues three paid insertions; double OK queues two paid insertions; it never refills an empty bank. Top ten local records store final
remaining chips, level, paid insertions, duration, seed and all 28 relic levels.
Ties favor higher level, then fewer paid insertions, then the earlier record.
A versioned, CRC-checked 260-byte NVS blob stores only records and discovery.
Writes run on the existing sound worker, outside input callbacks and the
simulation owner; init/commit/deinit releases the NVS cache between operations.
Storage failure never erases NVS automatically. Forced diagnostic awards mark
the current run as practice and exclude it from rankings. Reboot always starts
a fresh cabinet and draft, while records and discoveries survive.

## Twenty-eight stackable relics

| ID | Relic | Effect and scaling |
| --- | --- | --- |
| 0 | Magnetic attraction | Centering and forward force, increasing each level |
| 1 | Lunar gravity | Cabinet gravity reduced by 12 percentage points per level |
| 2 | Frost bed | Lower-bed friction reduced by 22 percentage points per level |
| 3 | Bounce candy | Increasing impact restitution; lunar gravity adds bounce |
| 4 | Tailwind rod | Rod speed +25% per level; drawing and contacts share its pose |
| 5 | Tidal pusher | Physical stroke +2 mm per level, to 41 mm |
| 6 | Coin echo | 15/30/45% chance of a free inlet coin per paid insertion |
| 7 | Ripple chain | Collection launches a local radial impulse, 12-tick cooldown |
| 8 | Wormhole return | 25/50/75% chance of a free coin on a siphoned collection |
| 9 | Stellar fusion | Every eight gold: 4/8/12 chips and 2/4/6 physical bonus coins |
| 10 | Space fold | 20/40/60% chance to rescue a side-lost gold through the inlet |
| 11 | Alchemy | 8/16/24% collection chance to queue a physical blue chip |
| 12 | Rainbow lamps | Intermittent second lamp / two lamps / all three lanes lit |
| 13 | Lucky dice | Jackpot odds rise from 1/16 to 1/13, 1/10, 1/7 |
| 14 | Hourglass | Event duration +25/50/75%; repeated awards accumulate up to twice that duration |
| 15 | Combo rhythm | Collections within three seconds build a capped 20-hit chip bonus |
| 16 | Chip interest | Reel completion pays 2/4/6% of the wallet, capped at 100 |
| 17 | Thrift coupon | Refund one insertion every 5/4/3 paid insertions |
| 18 | Chip exchange | Every 25/20/15 earned chips grants an insertion |
| 19 | Emergency insurance | Each acquisition adds one five-insertion rescue charge |
| 20 | Supply backpack | Each prize adds 1/2/3 extra insertions; acquisition adds three |
| 21 | Vending machine | Shop costs 18/16/14 chips; raises purchase allowance to 4/5/6 |
| 22 | Treasure map | Every 14/12/10 gold queues another physical prize |
| 23 | Mirror chest | 25/50/75% chance to repeat a prize's physical special event |
| 24 | Thunder ring | Every three reward events activates timed radial/lifting impulses |
| 25 | Antigravity field | Each reel completion levitates cabinet bodies for 3/4/5 seconds |
| 26 | Meteor forge | Every 5/4/3 side-lost gold queues a heavy chip dropped from 68 mm |
| 27 | Fate rewrite | 1/2/3 free rerolls per draft; favors stacking an owned relic |

Eight named synergies combine lunar+bounce, frost+thunder, wormhole+alchemy,
fusion+thunder, magnet+stroke, combo+interest, fold+meteor and map+mirror.
Some emerge from shared physics/economy; others strengthen a field, trigger
fusion lightning or accelerate treasure supply. There is no recursive spawn
inside the solver. Chip/prize queues cap at four/two, empty-space checks and
a 20-tick dispenser interval limit delivery, and the 112-body pool is retained.
Antigravity and magnetic forces stop at the actual cabinet boundary so escaped
bodies fall normally rather than hovering beyond the collection edge.

## Physical feed and play

All ordinary coins are bright gold tokens worth one virtual chip. Every
insertion starts at exactly (104, 26, 156) mm, without aiming. A 36 mm rod
pivoted at (86, 26, 130) swings ±0.32 radians every 2.4 seconds, followed by
13 pins and an inclined guide. Actual contacts and retained position/velocity
determine where the coin enters the shelf at Y > 72 mm; there is no assigned
exit lane or teleport to a random coordinate. Input spin varies slightly.

The table is 208 mm wide with a 145 mm front edge. Thick octagonal coins and
boxes rotate, stack, wake supported bodies and tip off finite ledges. Two
six-level towers initially carry red/blue prizes; green starts near the front.
The limit is 112 bodies and 576 contacts, budgeted together with sound DMA and
rendering. Missing prizes reserve available body slots. Full/blocked insertions
cost no credit and never delete bodies. Credits are free with no cash value.

## Events, prizes and jackpot

Three lamps at the actual guide exit rotate every two seconds. A paid coin
crossing a lit lane starts a 1.5-second reel animation, then reveals three
matching symbols for one second. Reels have a five-second cooldown. During
active play, a mystery draw also arrives after 12–18 seconds without another
draw; idle cabinets do not draw. The cabinet need not fill, nor reach a fixed number of paid coins, to trigger these draws. An eight-event shuffled bag avoids
repeating an ordinary result until the bag is exhausted; a 1/16 jackpot draw
can interrupt it. Collecting all three prize colors also gives a jackpot.

| Prize/event | Physical effect |
| --- | --- |
| Red / push rush | 10 seconds of a 2.1-second pusher cycle, retaining stroke and continuous phase |
| Blue / gold rain | Eight free gold coins through the same inlet/rod/pins |
| Green / side save | Physical lower gutter covers and rails for 15 seconds |
| Black hole siphon | Eight seconds of inward/tangential forces and an open center floor hole; bodies fall below the bed before collection, including prizes |
| Coin multiplication | Mark every current lower gold coin for one physical copy, released into clear overhead space; new paid drops also earn a bonus during 12 seconds |
| Side tower delivery | Queue two alternating side carriages, each containing two six-level gold stacks (12 real coins) |
| Earthquake | Five seconds of periodic horizontal and vertical impulses to break up piles |
| Fever | Ten seconds of faster pushing and double collection value |
| Jackpot | 50 chips, three insertions, twelve physical bonus coins, rush, side save, celebration, fanfare and backlight pulses |

Side towers also replenish every 24 seconds of active play (first opportunity
after 18 seconds). Each three-second carriage uses a moving side plate,
transparent front retainer and lateral guides. The side doorway/apron is open
during travel; all coins retain normal rigid-body contacts. Guides release at
the end, allowing towers to topple or push other coins. Occupied entrances or
more than 64 existing bodies defer delivery. Black hole completion queues fresh
side towers, rebuilding the cleared cabinet. At most four deliveries are owed.
No existing pile is reset to insert a tower.

Multiplication checks overhead clearance and releases one copy every six ticks,
only below 83 active bodies. If an original leaves before its copy can be made,
the owed copy joins the normal inlet queue. Copies are gold rigid bodies, not
score-only rewards. Rain and copies preserve the 4,096-coin owed queue and
prize slot reservation. Prize dispensers wait six seconds after absence, then
release a box at (46/104/162, 91, 48) mm when clear. All timers freeze on pause.
New effects may coexist; later awards do not truncate a jackpot celebration.

At 68 bodies an automatic circulation belt engages the lower bed at 18 mm/s,
with real contact surface velocity and friction. It disengages at 52, using
hysteresis. It also speeds the pusher and wakes supported piles. Every coin
still leaves through physical front collection or side loss; no capacity-based
deletions, teleporting payouts or resets are used. The larger pool supplies
headroom during rain while physical output catches up. Automatic paid feed interleaves with queued rain in Roguelike mode. `FLOW capacity_waits` distinguishes pool exhaustion
from normal inlet occupancy. Persistent sweep ordering and candidate bitsets
cull non-overlapping pairs while retaining original solver order, including
pairs that wake during contact generation. Smaller caches and shared prelit
material tables recover RAM; only cap polygons are cached between tiles.

## Chinese UI, materials and effects

A centered front orthographic camera has no yaw or roll. Per-pixel 3D depth
handles coins, boxes, pins and moving parts. The cabinet uses original generated metal/enamel artwork as native-resolution 256-color dithered tiles
(76,800 raw indices and a 512-byte palette). Runtime expands only dirty regions without LZ4 background decoding or another framebuffer. The 50,424-byte LZ4 data and 512-byte palette remain codec regression fixtures. Exact static depth uses 1,832 runs (23,280 bytes). Coins select
one of 1,552 offline ray-rendered poses from their actual 3D orientation:
97 normals (15-degree tilt, 22.5-degree azimuth) and 16 spins. The 488,058-byte
Flash atlas packs native silhouettes, 32 gold shades and per-pixel depth in
two-unit increments. All world motion, orientation and collision remain full
runtime rigid-body calculations; orientation quantization affects drawing.
Eight exact-pose checks compare atlas depth to the analytic prism and require
mean color-channel error below 12/255. Prizes retain mesh rendering and their
generated face packaging. Steel reflections use precomputed scanlines. Visible
raised bodies retain receiver-depth contact shadows and old/new dirty bounds.
Two 32-line buffers overlap SPI and physics; completed frame counts wait for
the final DMA completion. There is no extra runtime framebuffer or atlas heap.

The native UTF-8 renderer explicitly selects the checked 12/20 px, weight-600
Noto Sans SC subset under SIL OFL 1.1. All 505 inventory characters have glyphs;
source coverage is checked by `tools/bake_pusher_font.py`. This does not rely
on the default Montserrat font. Source images, prompts, font license/subset
and original generated WAV files are retained under `assets/`.

A separate worker streams original 16 kHz/16-bit mono coin, prize, jackpot, reel, event and siphon
PCM in 256-sample chunks through the BSP codec at volume 55. Game callbacks only
send notification bits. Jackpot audio has priority; reset cancels effects.
The user confirmed that the requested hardware LED is the LCD backlight:
jackpots pulse 65/100 percent and restore normal 85 percent afterward. No
independent LED GPIO is assumed. Battery reads share this background worker. Its 4,096-byte stack includes the
codec initialization/logging call chain; startup high-water marks are logged.

## Solver and checks

Quaternion rigid bodies, SAT face/edge checks, angular inertia, short contact
caches, four impulse passes and two support-normal sweeps run at 30 physics
steps per real second. Render cadence is measured independently. The stationary
bed/shelf/body friction is 0.156/0.547/0.25. Full rotating convex CCD, metal
flexibility and real optical reflections remain outside this game model.

Run `./tools/validate.sh` under ESP-IDF 5.5.3. Mechanical fixtures separately
disable gameplay effects to isolate support, impact, tipping, ledges, thin-cap
sweeps, fixed-inlet distribution and casual/dense returns. Gameplay tests cover
all three collected-prize effects, free coins without paid input, cover expiry,
lit-lane/reel scheduling, restock and four 120-second repeat cycles plus four
240-second arcade stress runs. The compact
format-8 snapshot helper is tested but is not used to resume the application.
Renderer tests compare 1,800 arcade plus 500 Roguelike full/dirty frames, all menu contexts, every codex entry, physical chips, guards, native detail and RGB565 byte order. Roguelike tests exercise all 28 relics separately and fully stacked, eight varied runs, zero-credit settlement, rescue, shop limits, rerolls, CRC corruption and ranking order.

USB commands: `status`, `drop`, `pause`, `auto`, `test N`, `shot`, `reset`.
Named diagnostics `event rush|rain|save|jackpot|hole|double|towers|shake|fever`
exercise the same reward/effect path without pretending to be natural draws.
`key up|down|ok|double|menu` uses the same owner input path; `relic ID`, `coins N`, `wallet N` and forced `event` mark practice. `finish` settles normally. `ROGUE`/`DRAFT` expose build, run and storage state.
`ARCADE` logs expose lamp/reel state, siphoned/cloned bodies and side deliveries.
`GAME` logs expose events, reel count, physical bonus count, restocks and
gold/copper counts. `coin_fx` logs initialization and completed PCM/pulse
requests. `shot` emits 240×320 RGB565_LE between `CP3D_FRAME` markers and
intentionally pauses game time. Logs and software captures cannot establish
physical LCD quality, speaker loudness, visible backlight response or player
satisfaction; those checks remain explicit.

## Current tactical-firmware acceptance, 2026-10-16

This section supersedes the historical firmware results below. The complete clean build, host tests and
merged-image/ELF verification passed under ESP-IDF 5.5.3. Firmware SHA256:
`bc834581dbf9a3334b32f140da7836b977c69ef7e86fd431c3ab4cb7014bb87f`.
Matching application ELF SHA256:
`9c5488173b37c2a0ae97f13f50c2778b8c8640c6078bda23914b948fb0733fc6`.
Local bundle: `build/firmware/bc834581dbf9a3334b32f140da7836b977c69ef7e86fd431c3ab4cb7014bb87f/`.
Evidence: `build/tactics-20261016-final-bc834581/`, including source binding, complete gate, 50-run
sanitizer audit, trial/final executable-payload comparison, USB round logs, startup checks and captures.

The measured board has embedded 8 MB XMC Flash. The application now uses QIO at 80 MHz after the
standard DIO ROM bootstrap. A bounded boot trial established support before changing the tracked default.
Bootloader, partition table and application were flashed at 0x0/0x8000/0x10000 with verified component
images and compatible partitions; NVS was not overwritten. The final gate-produced image passed three
hardware resets with its matching embedded ELF identity, QIO startup, valid state, preserved ranking and
30 fresh insertion credits. These resets are not a physical power-cycle test.

Fifty ASan/UBSan and contact-normal-audited host runs ended naturally: 45 wins, 33 runs with contact
combinations and nine with seed growth. No state error, contact-pool overflow or capacity wait occurred.
This is not a zero-intersection proof: the JSONL retains pre-solver contact-depth peaks from discrete arcade
stress. The solver does not implement general rotating convex continuous collision detection.

Two QIO device practice runs lasted 439.58 seconds: one win and one third-act timeout, without runtime
faults or ranking writes. All eight tactical types were actually cast. These two random runs did not trigger
a contact combination; earlier DIO device runs exercised combinations and seed growth. The trial and final
application executable payloads are byte identical outside the descriptor and checksum/digest; the final
image was separately flashed and checked after those runs.

Active-play windows measured **23.1 / 30.0 / 30.9 FPS minimum/median/maximum**; 32 of 43 windows
were at least 29 FPS. Deliberate USB capture pauses and menus are excluded. **Sustained 30 FPS is not met**
in heavy combined effects. QIO improves bandwidth, but does not remove the dense collision/rendering cost.

- Build: PASS.
- Host tests: PASS.
- Device tests: FAIL for the strict sustained-30-FPS target; boot, round flow and software captures passed.
- Unverified: physical button actuation, actual LCD appearance, speaker volume/timbre, visible backlight
  pulses, physical power cycle and player assessment of balance. Chinese text was checked in software captures.

The device is left at a new initial draft with 30 insertion credits, zero casts and autoplay stopped.

## Extended automatic testing, 2026-10-16 — physics quality FAIL

The delivered firmware was unchanged: ELF
`e641cca0442b3c0dcaeb54cd881ef1144b58f5c5810c68fd85a9d94a6ca81858`.
A USB controller selected cards, enabled paid autoplay and started the next run.
Six complete device runs used different card preferences, starting at 30 credits
with the practice flag, and settled at approximately 90 active seconds each.
No extra coins, relics or events were injected during play. Source hashes were
matched against the delivered build binding. Evidence and controller sources
are in `build/autoplay-20261016-584e0355/`.

| Card preference | Chips | Level | Paid coins | Logged display windows (Hz) |
| --- | ---: | ---: | ---: | ---: |
| physical | 1508 | 7 | 58 | 24.0–29.1 |
| lunar | 1633 | 8 | 62 | 22.5–30.8 |
| wormhole | 1651 | 8 | 63 | 19.4–27.5 |
| economy | 1258 | 6 | 61 | 21.1–27.2 |
| supply | 1575 | 7 | 56 | 20.1–30.4 |
| random | 1465 | 6 | 61 | 20.0–29.8 |

All six device runs were window-limited settlements, not natural game-over runs.
They had no panic, invalid body state, allocation failure, contact overflow or
capacity wait. Peak device contacts were 331/576. The old ranking remained at
854 chips/generation 19. One earlier lunar observation lost a status reply;
the controller stopped that observation, the game still accepted settlement,
and the complete lunar run was repeated with bounded status retries. The resumed
five-run batch required zero retries. Both original and resumed logs are retained.
The port is closed, and the device is back at the fresh starter draft.

Fifty 90-second host windows (4,404.6 simulated seconds total) selected all 28
relics and naturally formed all eight synergies. Six runs ended naturally;
44 still had a live build at the window limit. Final chips ranged 248–2,730,
median 1,267. Capacity waits and contact overflow were zero, maximum contacts
335/576. Fifteen shop purchases occurred. The same 50 cases completed under
ASan/UBSan without errors. This automated policy is not an optimal human strategy
and a 90-second window does not measure long-term survival probability.

**The new collision precision diagnostic fails.** Seed `20278729`, physical
preference, step `729`, bodies 17/18 reproduces a cached Q12 normal
`(2048,6144,2048)` of length 1.658312 and contact depth estimate 31.5508 mm.
Fresh SAT at the same pose gives normal `(2,0,-4093)`, length 0.999268,
and depth 0.398438 mm. Instrumentation preserves the original simulation result.
A non-unit/stale contact can amplify the impulse and may explain unnatural
kicks; the 31.55 mm estimate is not proof of that much literal geometric overlap.
Cache validity and normal precision need a regression fix before accepting
collision realism. This session records the failure without changing firmware.

Build: NOT RUN (unchanged verified firmware). Host tests: FAIL for collision
precision; general state and memory checks PASS. Device tests: FAIL against
locked 30 FPS, while six flow/stability runs PASS. Windows after at least ten
active seconds ranged 19.4–30.8 Hz, including draft pauses and diagnostic polling.
Unverified: physical buttons, panel/audio/backlight perception, player enjoyment,
and on-device reproduction/correction of the new precision issue.

## Roguelike delivery acceptance, 2026-10-16

- Build: PASS — complete ESP-IDF 5.5.3 gate in the matched local source mirror, merged layout and archive verification.
- Host tests: PASS — 28 relics separately and at all maximum levels, eight varied runs,
  economy/insurance/shop/draft controls, chip fever settlement, ranking/CRC tests,
  1,800 arcade plus 500 Roguelike full/dirty frames; ASan and UBSan pass.
- Device tests: PASS within the USB, log, DMA and captured-frame scope below.
- Unverified: physical ADC button presses, panel appearance/tearing, audible quality,
  visible backlight brightness and player satisfaction.

Final full image SHA-256:
`584e03559fdf1ce1eb8dcbf1d17ddab55e834c57247e4bc138924fbd4f208664`.
Matching ELF SHA-256:
`e641cca0442b3c0dcaeb54cd881ef1144b58f5c5810c68fd85a9d94a6ca81858`.
The verified debug bundle is `build/firmware/<full-image-sha256>/`; application
size is 1,300,400 bytes and merged size 1,365,936 bytes. Final flashing wrote
verified bootloader, partition-table and application segments to 0x0, 0x8000
and 0x10000, preserving the compatible NVS records. No whole-chip erase was used.
Local evidence is in `build/rogue-20261016-584e0355/`: acceptance JSON, source
binding at build time, logs, 15 device PNGs and the matching manifest.

The final run lasted about 173 seconds including captures and resets. Double OK
accepted exactly two paid insertions. The natural window accepted 46 total paid
coins, collected 163 bodies including seven prizes, reached level six and settled
823 chips. The previous 854-chip top record survived the update. The final run's
record, discovery state and historic relic build were saved/read after reset.
A practice run then applied every relic at level three (all eight synergies),
exercised siphon, multiplication, quake, side towers and fever, and bought five
insertions for 14 chips. That practice run did not change ranking generation 19.
Eight consecutive final USB resets each showed one expected USB reset, clean
audio startup, 30 insertions, zero score/paid/collected/events, 52 bodies and the
starter draft, while retaining the records/discoveries. The port was closed.
The cabinet was left at that fresh starter draft. Local rankings contain the
normal acceptance runs; forced-effect practice runs are excluded.

Maximum contacts were 288/576; contact overflow and pool-capacity waits were
zero. No panic, invalid body state, failed allocation, DMA timeout, PCM write
failure or NVS save failure appeared. Nine completed jackpot PCM requests
reported 16 backlight phases each. These are driver/log observations, not an
acoustic or optical instrument measurement. The diagnostic `ROGUE build` field
counts persistent discoveries; it does not mean those relic powers are active
in a fresh run. The codex and recorded levels show the actual build.

Moving frame preparation into Flash recovered internal RAM for the new state
and transient NVS cache. Final IRAM text is 131,762 bytes, DRAM data 7,304 bytes
and BSS 154,368 bytes. Runtime free heap was 16,516 bytes, logged minimum 6,600,
largest block 5,888; audio startup stack spare was at least 1,200 bytes.
**A locked 30 FPS is not achieved:** sustained play windows in this final seed
ranged 20.9–29.8 completed updates/s, with dense bouncing scenes slower.
Initial/static/menu transition windows also reported 0.2/13.7/16.4 Hz; these
count actual dirty updates while the game is partly paused. Earlier seeds
reached about 27–31 Hz, so that range must not be generalized to every build.
The 30 Hz fixed-step simulation and display cadence are distinct.

## Arcade event and side-tower acceptance, 2026-10-16

Build: PASS. Host tests: PASS. Device tests: PASS for the observations below.
Unverified: physical button operation, panel appearance/tearing, speaker loudness,
visible backlight response and subjective fun. The complete ESP-IDF 5.5.3 gate
ran in the matching native source mirror, with source hashes retained locally.
The new tests cover the lit lane, cooldown/pause, eight-event bag, idle behavior,
both physical side carriages, intact stacks during carriage travel, physical
copies, siphon collection, fever payouts and malformed snapshot metadata.
Four 240-second arcade runs delivered 17–19 carriages, with no capacity wait or
contact overflow. The existing 128-coin rain fixture emitted 306 bonus coins
including subsequent awards, with zero capacity waits. Address/undefined-behavior
sanitizers passed for the arcade logic and the 1,800-frame dirty/full renderer.

An initial device probe exposed insufficient RAM for audio DMA and the game
task. Rare gameplay/side-carriage/render helpers were moved out of the IRAM
solver path, and a duplicated drawing specialization was prevented. Relative
to that probe, IRAM text fell from 143,950 to 137,288 bytes (6,662 bytes saved).
The final exact firmware passed eight fresh USB restarts with audio ready and
1,504 bytes of audio-worker stack remaining. Runtime free heap was stable at
11,888 bytes; the startup minimum was 1,976.
The final 90-second natural-play test used the actual paid inlet plus extra
paid inputs. It accepted 73 paid coins and 48 bonus coins, collected 270 bodies
including 12 prizes, refilled 12 prizes and delivered six side carriages
(72 physical gold coins). Ten natural reel results were recorded. The first
eight were multiplication, siphon, towers, earthquake, side save, push rush,
fever and rain, with no repeat. Four jackpots occurred through prize collection.
The siphon counter reached 48 and the clone counter 86; the latter includes
copies transferred to the normal inlet queue when an original leaves.
Capacity waits remained zero, peak contacts were 297/576 and all ordinary coins
remained gold. No panic, invalid state, DMA failure or failed PCM write occurred.

A separate controlled phase directly invoked effects to capture the black hole,
left and right carriage travel, multiplication and jackpot. Those invocations
are not counted as natural reel results. Nine RGB565 device frames include
rolling reels, stopped matching symbols and a final fresh cabinet. Event, reel
and siphon PCM completions were logged; jackpot audio/backlight requests completed
with 16 pulses. The device was left at 120 credits, 52 bodies and reset arcade
counters; the serial port is closed.

Completed update windows ranged **21.8–30.9 Hz**; dense scenes still do not lock
to 30 FPS. Physics remains at 30 fixed steps per real second. Chinese font
coverage is 107 code points at 12/20 px. Controls and the centered native camera
remain the application-specific arcade interface, with lit-lane status replacing
the former fixed-24 progress bar.

- Version: `coin3d-arcade-20261016`
- Exact archive: `build/firmware/36560f6140a76a1ae963ed978e497d6b60d05f7ff9c88ad85f8314caeb889904/`
- Merged SHA-256: `36560f6140a76a1ae963ed978e497d6b60d05f7ff9c88ad85f8314caeb889904`
- Matching ELF SHA-256: `803c07739b13a6abd87b158f354523741bd79335b9a5a91e605d58c810dfc1fb`
- Evidence and nine frames: `build/arcade-20261016-36560f61/`
- Merged image: 1,285,440 bytes at `0x0`; application 1,219,904 bytes in the
  existing 8 MB minimal partition layout. No full-chip erase was performed.

## Previous capacity and jackpot acceptance, 2026-10-15

This section supersedes the historical device results below. Build and host
tests passed the complete ESP-IDF 5.5.3 gate in the matching native source mirror
`/tmp/coin3d-runtime/source`; iCloud file eviction had stalled direct workspace
builds. Materials, Chinese glyphs and audio exports reproduced byte for byte.
Physics and renderer address/undefined-behavior sanitizer checks passed.
The 128-coin rain fixture physically emitted 176 bonus coins, including later
awards, without capacity waits or contact overflow; four repeat-game seeds
also had zero capacity waits. Coins are retained until physical front/side exit.

The exact verified merged image below was flashed at `0x0` to the confirmed
ESP32-C3 USB device. Eight bounded USB restarts each began a fresh 120-credit,
52-body cabinet, with no snapshot restore. An observed audio-initialization
stack fault was corrected by increasing that worker from 2,560 to 4,096 bytes;
the final eight starts retained at least 1472 bytes of stack. Stack protection
remains enabled. A simultaneous prize award no longer erases the jackpot
banner; its physical effect still applies, with a host regression check.

The final bounded observation ran a 90-second paid-chute test plus extra paid
inputs and captured route, jackpot, dense and fresh-cabinet RGB565 frames.
It recorded 106 paid coins, 51 bonus coins, 164 collected
bodies including 7 prizes, 7 refills, 5 jackpots
and 4 circulation activations. Capacity waits remained
zero; peak contacts were 308/576, and minimum free heap was
3608 bytes. All ordinary coins were gold (copper count zero).
No panic, invalid state, contact overflow or PCM-write failure was observed.
Completed PCM and 16-pulse jackpot backlight requests were logged; the software
captures show the Chinese UI and the jackpot banner. The serial port is closed,
and the device is left in a fresh round.

Completed update windows ranged **21.1–29.8 Hz**. Dense collisions and
celebrations still have frame-time variation; a locked 30 FPS is not established.
Physical button operation, LCD appearance/tearing, speaker loudness, visible
backlight response and player assessment remain unverified. Logs and software
captures do not establish those physical results.

| Check | Result |
| --- | --- |
| Build | PASS: complete gate, partition/merged-image checks and exact archive verification |
| Host tests | PASS: mechanical, capacity/rain, prize/event/refill, renderer, decoder and sanitizer checks |
| Device tests | PASS for verified flashing, fresh restarts, bounded feed, counters and captured frames |
| Unverified | Physical controls, panel/speaker/backlight appearance and subjective playability |

- Archive: `build/firmware/170e7bb6a20e55e8d3799426e955efda1555dcd17df0922d76740a498a368929/`
- Full merged image SHA-256: `170e7bb6a20e55e8d3799426e955efda1555dcd17df0922d76740a498a368929`
- Matching ELF SHA-256: `b99f698b9ae77be7ebb548488fde09d1b387029f32f00289a8bb2f9d47734ed9`
- Local evidence: `build/jackpot-final-170e7bb6/jackpot-final-acceptance.json`
- Device frames: `build/jackpot-final-170e7bb6/device-jackpot-final-*.png`
- Asset sources and prompts: `assets/images/coin-pusher-realistic-v1/`; original
  cabinet/material source images used the built-in image generation tool.

## Previous performance iteration on 2026-10-15

Retained for traceability. The player rejected its coarse enlarged image and
resistant piles; the native-resolution/material-friction revision supersedes it.

| Check | Result |
| --- | --- |
| Build | PASS: complete `./tools/validate.sh`, ESP-IDF 5.5.3, merged image and archive verification |
| Host tests | PASS: repository checks, all host tests, final address/undefined-behavior sanitizer runs |
| Device tests | PASS: verified USB flash, startup, 90-second paid-chute stress, two device frames, restart/save restoration |
| Unverified | Physical buttons and panel appearance/tearing; player assessment of realism |

Full 80-body windows measured 30.5–30.9 completed updates/s. Physics advanced
in real time at 30 Hz. The run paid 54 coins, collected 20 bodies (including all
three prizes), lost six through side channels and earned 160 points. Peak
contacts were 446/640, minimum free heap was 22,420 bytes, and observed work
intervals occasionally reached 53.5 ms. These measurements establish average
throughput; short frame-time variation remains observable. No panic, invalid
state, contact overflow or failed save was observed. Device-generated RGB565
frames were inspected. After testing, a fresh 52-body cabinet with 120 credits
was saved and restored across restart; the serial port was released.

The complete gate ran in `/tmp/coin3d-runtime/source`, a matching source mirror
outside iCloud Documents, where source eviction had stalled local builds. The
application sources and dependency lock match the workspace. Generated image
and debugging artifacts are retained under
`build/firmware/81e995a7bbf14c43b1fa73d7b4cedd6e12463833fa86c60e23d8341b2c88861c/`.

- Merged image SHA-256: `81e995a7bbf14c43b1fa73d7b4cedd6e12463833fa86c60e23d8341b2c88861c`
- Matching ELF SHA-256: `e11f89a99ca8a8fdf48571c523332eab10565a4a590b29c5a81fba4f9d420397`
- Merged image: `build/firmware/81e995a7bbf14c43b1fa73d7b4cedd6e12463833fa86c60e23d8341b2c88861c/FoloToy-AI-Passport-full.bin`
- Local acceptance summary: `build/final-acceptance.json`

## Native-resolution and playability revision, 2026-10-15

The complete ESP-IDF 5.5.3 gate passed in the matching native source mirror.
Host checks include native pixel detail, full/dirty image equality, low-friction
landing, tower collapse, three-layer forward transmission and casual play.
Address/undefined-behavior sanitizer checks passed for physics and rendering.

The exact image below was flashed and verified over USB. On-device acceptance
used 20 single paid drops, a 60-second paid-chute dense run, three complete
RGB565 frames and a fresh-cabinet restart/save check. The single-drop case
collected 9 coins and 2 prizes, with
0 side losses. The dense run accepted 57 paid drops and
collected 26 bodies including 3 prizes; side losses
were 4. Completed frame-rate windows ranged 28.0–30.9 Hz,
with occasional work intervals up to 57.8 ms. This is measured
throughput, not a guarantee of a locked 33.3 ms frame time. Peak contacts were
421/640 and minimum free heap was 18,004 bytes. No panic, invalid
state, contact overflow, DMA failure or failed save was observed.

Build: PASS. Host tests: PASS. Device tests: PASS for the observed USB, serial,
frame generation and restart checks. Unverified: physical buttons, actual panel
appearance/tearing and player judgment of realism/difficulty. The cabinet was
returned to 52 bodies and 120 credits, automatic insertion off; restart restored
that snapshot and the serial port was released.

Merged SHA-256: `fea50cf3eda14f7452bb0bb5e5a87f5f0ce01f3b14ed5c35446006ed05f87b1f`

Matching ELF SHA-256: `913c9fe1b94b79f9cc79a4cf383d46b0e152c1a96946998f5ce6b4c7ea947cb6`

Archive: `build/firmware/fea50cf3eda14f7452bb0bb5e5a87f5f0ce01f3b14ed5c35446006ed05f87b1f/`

Merged image: `build/FoloToy-AI-Passport-full.bin`

Local evidence: `build/play-final-validation.log`, `build/play-final-device.log`,
`build/play-final-restart.log`, `build/play-final-acceptance.json` and
`build/device-play-*.png`. Frames were generated on the device, not photographed
from the physical LCD.

## Fixed-inlet acceptance, 2026-10-15

Build: PASS — complete ESP-IDF 5.5.3 gate in the matching source mirror.
Host tests: PASS — all repository tests and physics/render address/undefined-behavior checks.
Device tests: PASS — verified flash, fixed-inlet single insertions, 60-second
mechanism stress with extra insertion requests, four complete device frames and
save/restart restoration. Physical buttons, actual panel appearance/tearing and
the player's realism/difficulty assessment remain unverified.

The 20-single-insertion case collected 7 coins and 0 prizes.
At the end of the dense run, 61 paid insertions collected 29 bodies
including 3 prizes, with 4 side losses. Feeder counters were
60 rod contacts, 142 pin contacts and 60 completed transfers;
left/center/right exits were 14/21/25. The simulator uses
collisions to generate these exits; it does not allocate a random landing lane.
Displayed-update windows measured 28.3–30.1 Hz, with occasional work
intervals up to 73.3 ms. This does not guarantee a locked 33.3 ms frame time.
Peak contacts were 404/640, minimum free heap 10,716 bytes. No panic, invalid
state, contact overflow, DMA fault or failed save was observed. The cabinet
was restored to 52 bodies, 120 credits and automatic insertion off; restart
restored that snapshot and the port was released. Frames are device-generated
images, not photographs of the physical LCD.

- Full image SHA-256: `362173557801057136d3abdb5b6d1454d0f3e5b74dcafc0fca943f3301cdd265`
- Matching ELF SHA-256: `73f878ef5a8668285669584084dcd2d51f9fe14bf883fd38575d0a24b6ef8bc0`
- Archive: `build/firmware/362173557801057136d3abdb5b6d1454d0f3e5b74dcafc0fca943f3301cdd265/`
- Merged image: `build/FoloToy-AI-Passport-full.bin`
- Evidence: `build/feeder-final-validation.log`, `build/feeder-final-device.log`, `build/feeder-final-restart.log`, `build/feeder-final-acceptance.json`, `build/feeder-final-source-hashes.json`, `build/device-feeder-*.png`
