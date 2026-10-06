<p align="right"><a href="arcade-device-tests.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Odds Arcade Remote Device Tests

The `feature/tiny3d-physics` development configuration enables
`CONFIG_ARCADE_USB_TEST`. Disable it in `sdkconfig.defaults` for a build that
must not accept development commands. This interface is local USB only and
never writes persistent settings or initiates networking.

The LVGL pool is 96 KB. Dice and roulette use a shared 200×168 RGB565
canvas (67,200 bytes of static RAM), without transformed ARGB dice layers.
Check peak LVGL usage, largest free block and minimum system heap on the board
when changing the views or adding objects.

USB commands enter the same input queue as physical button events:

| Command | Action |
| --- | --- |
| `u`, `d`, `o` | UP, DOWN, OK click |
| `U`, `D`, `O` | UP, DOWN, OK hold |
| `m` | Measure actual refreshes during active game animations for 10 seconds; USB command processing waits until sampling completes |
| `v` | Label rectangle clipping/overlap audit and cube mesh validation; slot strip labels intentionally clipped inside the game frame are excluded |
| `?` | FPS display flag, dice count, neon mode, commanded backlight level, page, selection, animation, autoplay, speed, sound, completed rounds, heap, LVGL free memory, USB task stack watermark, audio readiness, successful PCM writes, audio errors, peak LVGL usage and largest free pool block |

Commands are case sensitive. Game/page operations still execute through the
input worker under the LVGL lock. State inspection also holds that lock.
The test does not simulate voltage changes at the physical buttons.

After the complete `./tools/validate.sh` gate, verify the exact firmware archive
with `tools/archive_firmware.py verify`. With authorized USB device access,
flash the retained component images at the offsets in its `flash_args` when the
existing partition table is compatible. This preserves NVS by avoiding the
merged image's padded data regions; never infer offsets from file names.

Run with the activated ESP-IDF Python (provides pyserial):

```bash
python tools/test_arcade_device.py --port /dev/cu.usbmodem101 \
  --log /tmp/arcade-device-test.log --reset
```

The bounded test defaults to `--rounds 70` autoplay completions per game.
It covers settings, all three speeds for slots/roulette/plinko, all six dice
counts, sound toggles, 10-second FPS windows, layout audits, returning during an
active animation, and 40 additional enter/play/exit cycles. It validates dice
count/face/total consistency and interior Plinko destinations. Do not press
physical buttons while the automated test owns the input sequence.

On home, hold OK to enter settings; UP/DOWN select a row, OK changes its value,
and hold OK returns home. Settings include FPS display (default off), dice count
(default two), neon effect, sound and speed. In the dice game, short UP changes
count when idle. Other games use short UP to cycle speed. Hold UP toggles autoplay,
short DOWN toggles sound, and hold OK returns home. Backlight starts at 100%.
Settings are currently volatile and reset at boot.
It fails on missing state responses, game timeouts, detected runtime exceptions,
queue overflow, and excessive retained memory loss. Logs remain local and must
be reviewed and sanitized before sharing. The test leaves the device at home.

These checks establish execution of real UI, animation and audio code on the
board. They cannot establish panel appearance, audible quality, actual button
contacts, or electrical measurements. Report those separately as unverified.

## Historical slot-machine regression

On 2026-10-06, ESP32-C3 with 8 MB Flash and no PSRAM passed the complete gate
and the USB test with `--reset --auto-seconds 30`: 72 completed rounds across
four games, all three speeds, sound toggles, autoplay, busy exits and 40 further
page cycles. No restart, watchdog, allocation warning or audio error appeared.
Minimum system heap was 140,996 bytes; LVGL peak usage was 56,652 bytes.
The home screen's system heap was unchanged at 145,348 bytes after testing;
LVGL free memory changed from 84,224 to 84,216 bytes (8 bytes retained).
There were 3,171 successful PCM writes and zero audio errors.

Verified full-image SHA-256:
`614cf5f38ad8aba7ed45b1d0c337491f04c99f55d8f5c158e1caa231d9d70f42`.
Matching ELF SHA-256:
`7a1a4c3f13dcbd7d8a545967d0859d42e2e36f819572696e2cdfaf99049f55fa`.
The device received the matching archived bootloader, partition table and app
at `0x0`, `0x8000` and `0x10000`, with NVS outside the written ranges.
Screen appearance, audible quality and physical button actuation remain
unverified; USB events exercise the application input path.


## Chinese UI and physics repair regression — 2026-10-06

This revision adds a full perimeter neon ring with three effects, Chinese fonts
and separated text rows, continuously moving slot strips, a numbered European
roulette wheel, one to six true cube dice viewed from above, and faster Plinko
with normal-based peg collisions. Dice projection and visible-face selection
now use the same camera basis. A 100-pose solid-silhouette test passes; a local
negative control using the previous projection fails that test.

Build and host tests: PASS (`./tools/validate.sh`, ESP-IDF 5.5.3). Host Plinko
checks ran 1,000 seeds for each of three speeds; all nine bins were occupied and
more than 80% of balls landed in interior bins for each speed.

Device: ESP32-C3, 8 MB Flash, `/dev/cu.usbmodem101`. Authorized merged write at
`0x0` completed with esptool hash verification. Full image SHA-256:
`1f45ec56b619166949d6f6d1ad3370ac67e3e12de119d2e94e5403112706de65`.
Matching ELF SHA-256:
`4184a8fdb2b0232f74654c6c4ed4b0509cc05b12bc9766942e93ee69ceea04c3`.
The prior working full image remains locally archived under
`462667987e48940eb6ec424ac2220234039beea2d7fb439e7217763f4c2b67a7`
and its archive verification passes.

USB acceptance: PASS. Main regression completed 299 rounds: slots 74, roulette
74, dice 77, Plinko 74, including at least 70 autoplay rounds per game and 40
additional enter/play/exit cycles. All 77 dice rounds settled naturally with
valid faces and matching totals. All 74 roulette final pockets matched the
reported numbers. Plinko bin counts were `[0,6,13,8,19,15,7,5,1]`; average drop
was 2,392 ms, maximum 3,666 ms. Font audit covered 108 codepoints in two fonts
with zero missing glyphs and a passing negative check. Label rectangle audits
passed on home, settings and all games. These audits do not prove LCD appearance
or glyph ink layout.

An additional FPS overlay test passed on all four games, bringing completed
rounds to 317. No unexpected restart, panic, watchdog, allocation failure or
audio error was recorded. System heap remained 76,696 bytes; minimum heap was
72,332 bytes; final LVGL free memory was 78,972 bytes (20 bytes below the initial
home view); peak LVGL use was 31,248 bytes. Successful PCM writes totaled 13,000.
FPS observes display refreshes and does not change game inputs, random results,
or simulation timing; drawing the badge does consume rendering time.

| Game | FPS display off | FPS display on |
| --- | ---: | ---: |
| Slots | 12.52 | 11.46 |
| Roulette | 6.54 | 6.38 |
| Six dice | 19.86 | 19.74 |
| Plinko | 43.52 | 41.49 |

Each value is one approximately 10-second window of completed display refreshes
while animation was active, with sound enabled and fast speed. Different random
rounds were used; this is not a controlled performance benchmark. Roulette
refresh remains low and its visible smoothness requires user acceptance.
Panel appearance, audible quality, physical button contacts and measured
backlight output remain unverified. Raw serial logs remain local. The test
returned to home with FPS display off and commanded backlight at 100%.


## 2026-10-06 multiline slots — manual acceptance handoff

Build and host tests: PASS (`./tools/validate.sh`). Verified merged image SHA256:
`3412c902d6a738c949985d5978c0e3ff0923ac9e8fa81aad24f5852de7ac23ea`.
Matching ELF SHA256:
`dad91be2bcc59c9912c5d8d75e793f82ab6b184674aa2b1778809bee929a91f2`.
Archive: `build/firmware/3412c902d6a738c949985d5978c0e3ff0923ac9e8fa81aad24f5852de7ac23ea/`.
The previously accepted classic-symbol firmware remains archived as
`0a66a106aa0332f6b52391a44658f16570d97c28582708d4e2189c7b98a82d9b` for rollback.

Settings now has six rows; hold Confirm to enter settings and select
Slot mode → Five-column multiline. Default remains Classic three-reel. Multiline slots use five
columns and three rows with continuous strip movement and 27 three-symbol
patterns: nine horizontal, six diagonal and twelve closed triangles. Horizontal
triples award 20/25/35/45/60/100 by symbol; diagonal and triangle patterns award
twice that. With no winning triple, matching leftmost pairs award five points per
row. Classic triples use the same base awards and any pair awards five; classic
symbol weights remain unchanged. Both modes show round points and separate
cumulative points; these and settings are RAM values and reset on restart.
Winning multiline rounds pause to show the highlighted paths before autoplay.

Host checks cover all 27 paths, 120 strip plans preserving visible symbols, and
100,000 deterministic seeds. Multiline scored rounds: 71.482%; triple-pattern
rounds: 52.169%; pair-only rounds: 19.313%. These are simulated statistics.
Classic sprites are 48×64; multiline sprites are 32×36, shared RGB565 assets.
The user's earlier LCD acceptance applies to classic symbols, not new multiline
layout. Layout previews are host illustrations, not LCD photographs.

The serial state reports slot mode, round score, separate totals and settings
row. Text audits now include slot labels and intrinsic clipped-text bounds.
The optional automated script supports both slot modes plus three other games
(350 autoplay rounds at 70 each), but this build is handed to the user for
manual gameplay acceptance; that automated suite was NOT RUN on this image.

Device flash and bounded startup: PASS on `/dev/cu.usbmodem101` (USB 303A:1001, MAC [device identity retained locally]), merged image at 0x0; esptool verified the written hash. After the intentional reset, the matching ELF prefix was reported, 124 glyphs had no missing entries, home text audit had zero clipping/overlap, audio initialized without errors, backlight command was 100%, FPS display was off and slot mode was classic. Serial is closed and the device is left on home. New-mode LCD appearance, physical gameplay, audio perception and animation FPS remain unverified pending the user's manual test. Local logs: `/tmp/arcade-multi-release-validation.log`, `/tmp/arcade-multi-flash.log`, `/tmp/arcade-multi-startup.log`.


## 2026-10-06 simultaneous Plinko balls

Plinko now accepts 1–10 balls (default one) in the seventh settings row.
When idle on the Plinko page, Up cycles the count; Confirm releases every
selected ball on the same simulation tick. Each ball has its own seed and
trajectory, with central release positions spaced 12 pixels apart. Peg and
wall collisions use the existing solver; ball-to-ball collision is not modeled.
The round completes once every ball lands. Occupied bins are highlighted,
landed balls are arranged in their bins, and the status displays landing progress.
Count selection uses RAM and resets to one after restart. Speed remains in settings.

Host simulation covers all ten counts at all three speeds, 100 seeds per case,
simultaneous launch, deterministic initialization, count limits and valid final
bins. The updated optional device script understands per-ball logs and checks
all ten counts when testing Plinko. Per the user's request, gameplay is reserved
for their manual test after flashing; no automated gameplay suite is run.

Build: PASS. Host tests: PASS. Merged SHA256 `1ef0f63ab6e62da6454334d4dd41963971b929e65187bd2195d6efeaf89df1b0`; matching ELF SHA256 `d58eb6fde80a05e3913dc1a2d39306904dd11976ae3675b77ed3161849096fea`. Verified bundle: `build/firmware/1ef0f63ab6e62da6454334d4dd41963971b929e65187bd2195d6efeaf89df1b0/`. Device flash/startup: PASS on `/dev/cu.usbmodem101` at 0x0, 930,560 bytes, write hash verified. Startup reported matching ELF, 127 glyphs with no missing glyphs and a passing negative check, ready status and no runtime errors. Home audit had zero clipping and overlaps. Initial observed Plinko count was one, FPS display off and backlight command 100%. Button activity overlapped the serial navigation check: its expected home/settings sequence assertion failed, so settings audit is not claimed. A subsequent state read confirmed home, idle, five selected balls, no completed game and zero audio errors. Serial closed. Gameplay and visible multicount animation remain pending user acceptance. Local logs: `/tmp/arcade-plinko-multi-validation.log`, `/tmp/arcade-plinko-multi-flash.log`, `/tmp/arcade-plinko-multi-startup.log`. Previous multiline image `3412c902d6a738c949985d5978c0e3ff0923ac9e8fa81aad24f5852de7ac23ea` remains verified and available for rollback.
