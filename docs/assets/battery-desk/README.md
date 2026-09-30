<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Battery Desk and Blue

A local battery collection and care companion for the FoloToy AI Passport.
Source lives on `feature/battery-assets`, originally created from `main`.
The embedded web server, complete inventory, audit history, reminder settings and
British Shorthair blue cat's growth all live on the device. No external database,
cloud account, CDN or Internet connection is needed. Hardware APIs remain in
`components/bsp`; application logic and both redesigned interfaces are in `main`.
The baseline demo menu and pages are not compiled into this application.

## Inventory without an arbitrary total limit

The previous 16-asset total restriction is removed. **16 is a page size**, so RAM
usage stays bounded. Web and device can browse successive pages; web search and
status/attention/reminder filters scan the entire inventory. Dashboard counts
cover all assets; the charge chart explicitly covers the current page. CSV exports
all assets regardless of filters. JSON exports all assets and all audit entries,
plus the cat's growth and reminder preferences; there is no restore/import endpoint.
The history page shows the latest 48 entries, while older history stays in Flash.

Physical capacity is finite: this board has **8 MB Flash**, with a **0x4f0000-byte
(4.94 MiB) LittleFS partition** reserved for assets and history. The firmware
partition is 3 MiB. The UI displays actual filesystem usage. The effective number
of assets depends on filesystem overhead and growing history; it is not a promised
unlimited capacity or a fixed number of rows. A 32 KiB reserve protects ordinary
writes from consuming all free space; exports and deletions remain available.
IDs are stable and not reused. Exhausted space rejects writes without evicting
assets. Large collection scan latency and actual full-storage behavior need board
measurement; host tests demonstrate 140 records, not a hardware capacity benchmark.

## Use it

1. Hold `OK` on Blue's home screen, then click `OK` to start the workspace.
2. Join `BatteryDesk-XXXX` using the generated key shown on the device. Keep the
   connection when the phone warns that it has no Internet.
3. Open **http://192.168.4.1/**. The phone sends its real Unix time and timezone
   automatically on open, refresh, foreground return and about once a minute while
   visible. The device advances that time with its monotonic clock.
4. Add assets, record their charge/health and optional next-check date, and choose
   the charging-check interval (1–1440 minutes; default 120).
5. Open an asset's details to check out, return, charge, finish charge, service,
   finish service or retire it. Retirement is required before deletion.

The top-right screen percentage is the Passport's own optional CW2017 reading.
Inventory charge and health are manually entered. Completing charge records 100%
and increments cycles; confirm actual charging has finished first. This firmware
does not electrically measure, switch or charge the recorded batteries.

| Device screen | UP / DOWN click | OK click | OK hold |
| --- | --- | --- | --- |
| Blue home | Reminders / asset cards | Pet Blue | Phone workspace |
| Asset cards | Previous / next asset across pages | Choose action | Blue home |
| Action picker | Select; cancel pending confirmation | Select, then confirm and save | Asset card |
| Reminders | Browse due assets across pages | Snooze sounds for 15 minutes | Blue home |
| Phone workspace | Blue home | Start / stop Wi-Fi | Blue home |

Non-OK holds and double clicks have no assigned action. The backlight dims after
45 seconds and turns off after 120; the first press wakes without executing an
operation. Starting Wi-Fi is explicit, supports one client, and generates a fresh
12-character WPA2 key at every start. Returning home does not stop the server.
Deep sleep is not implemented. Device text uses supported English Montserrat fonts
and ASCII `BAT-xxx` identifiers; full UTF-8 names remain in the browser and exports.

## Gentle reminders and growth

Active assets produce low-charge reminders at ≤20%, a charging check after the
chosen interval, an optional scheduled check, or a status update after seven days.
The earliest applicable reason is shown. A charge begun without known time is
shown immediately for checking rather than inventing its start time. Retired assets
are excluded. There is no estimate that charge is physically complete.

The browser has a reminder center, a due counter and an all-due paginated filter.
The terminal shows the counter and lets you browse every due asset. The speaker
worker synthesizes a short rising cue, with a 30-minute repeat cooldown. Sound
volume (0–60), mute, quiet hours (default 22:00–08:00) and a 15-minute snooze are
persisted. Quiet start=end disables the quiet-hours window. Visual reminders remain
visible during mute/snooze. Sound is disabled until the phone has synchronized the
clock. Speaker initialization failure is nonfatal and reported. Closing the browser
stops browser updates; there are no background OS push notifications. The powered
terminal continues checking reminders after the phone disconnects.

Blue is a rounded blue-grey British Shorthair with amber eyes, a blink animation
and a happy hop. Real care earns growth: completed charge +10, completed service
+8, meaningful metadata update +5, return +2. Each asset/action rewards once per
local calendar day, with 40 total points per day. Unknown-time actions and repeated
unchanged edits give no reward. Petting triggers a friendly response without points.
Levels unlock at 0/60/120/180 points, adding a collar, crown and heart in the web UI;
the terminal has a collar and gold decoration. Growth and streak survive power loss.
Rewards accompany user-recorded care, not independently verified charging; never
charge a battery just to earn points.

## Persistence, migration and time

LittleFS stores one CRC-checked asset file per ID, a metadata file and an append-only
CRC-checked audit file. A durable intent contains the new asset, audit entry and
metadata. Saves sync the intent, update the asset, append history once, replace
metadata atomically, then remove the intent. On reboot an interrupted durable
transaction replays idempotently before the archive becomes available. A request
failure can leave the result unknown: inspect state/history after recovery before
retrying. Revision conflicts reject stale edits and preserve browser drafts.

Only an entirely erased new assets partition can be automatically formatted.
A mount failure on nonblank bytes, corrupt legacy NVS, unknown metadata or a corrupt
pending transaction fails closed without erase. The device still presents its
storage error and workspace screen. Initialization never erases unrelated NVS.
Existing `battery_v1` NVS records migrate with stable IDs and retained old history;
the source remains untouched. Progress markers make migration resumable and prevent
deleted old assets from resurrecting on later boots.

Cold boot starts with unknown current time: Flash timestamps cannot tell how long
power was absent. Offline operations save with epoch 0. Phone sync never invents
historical dates. Absolute schedules, growth, quiet hours and timed speaker checks
need fresh phone time after each power cycle. Valid dates span 2024–2100, timezone
−12 to +14 hours. Time accuracy depends on the phone's clock.

Names/locations/notes allow validated UTF-8 up to 63/47/95 **bytes**. Capacity is
1–60000 mAh, percentages 0–100 and cycles 0–65535. Status edits cannot bypass the
audited state machine. JSON bodies are bounded to 1536 bytes and reject unsafe
payloads, duplicate fields and foreign request origins. The AP key controls access;
this is a private local workspace without accounts or Internet hosting.

## Build, browser verification and flashing

Use ESP-IDF **5.5.3** and the five required Passport skills:

```bash
source <esp-idf-v5.5.3>/export.sh
./tools/validate.sh
python3 tests/battery_web_preview.py --seed
# Synthetic preview: http://127.0.0.1:8765/
NODE_PATH=<directory-containing-playwright> node tests/test_battery_web.cjs
```

The full gate covers repository checks, model/protocol tests, real file-engine
140-record pagination, four interrupted-transaction stages, CRC rejection, migration,
reward caps, quiet hours, ESP wrapper mount/space/lock failures and existing BSP
button/audio recovery tests. The browser fixture executes the production C archive
on a temporary host filesystem; it is not a board emulator. Browser tests cover
33 assets, all-due navigation, global search, exports exceeding 48 entries, cat reward
behavior, settings/snooze, CRUD, conflicts, escaping, reconnection and 1440/390/320
pixel layouts. Screenshots are synthetic and saved under `build/previews/`.

The verified `build/FoloToy-AI-Passport-full.bin` is flashed at **0x0**. Its matching
ELF/MAP and segmented images live in `build/firmware/<full-image-sha256>/`. Verify the
bundle with `python3 tools/archive_firmware.py verify <bundle>`.

**For upgrading the prior Battery Desk while retaining NVS for migration**, export
first and use the matching bundle's segmented `flash_args` (bootloader 0x0, partition
0x8000, application 0x10000). Do not flash the merged image over old NVS: its padding
can reset that data. New layout: NVS 0x9000/0x6000, PHY 0xf000/0x1000, factory
0x10000/0x300000, assets 0x310000/0x4f0000. Once on this layout, compatible segmented
updates leave both data partitions untouched. No filesystem image is flashed.
Never use the application-only binary at 0x0 or perform a full-chip erase as an
upgrade step. Nonblank incompatible assets bytes require an explicit recovery
choice; the app will not silently erase them. See [firmware layout](../../development/engineering/firmware-layout.md).
For an optional render of the actual terminal widgets (after IDF resolves LVGL):

```bash
cmake -S tests/lvgl_preview -B build/screen-preview
cmake --build build/screen-preview
mkdir -p build/previews
build/screen-preview/battery_screen_preview build/previews
```

This uses RGB565 at 240×320 and the configured 32 KiB LVGL pool, checks available
allocator space, and writes PPM frames for home, happy, assets, actions, reminders
and Wi-Fi. It does not validate the physical LCD or SPI. Single-line labels have
explicit heights; multiline descriptions and key hints have bounded heights.

USB flashing requires explicit consent and access to the user's computer/USB bridge.

## Physical acceptance — NOT RUN

A build, host test or browser screenshot is not hardware acceptance.

| Item | Procedure | Acceptance |
| --- | --- | --- |
| UI and three keys | Visit every screen; test click/hold, wake, page boundaries and stale confirmations | New UI only, readable 240×320 layout, no clipped controls or duplicate operations |
| Web and clock | Android/iOS join AP; open/foreground/refresh; compare local time | Page loads, clock and timezone match, CRUD updates terminal; reboot waits for sync |
| Larger inventory | Create ≥140 assets and >48 operations; search last ID, browse both directions and export | All assets reachable; full export and counts agree; measure response latency |
| Power interruption | On expendable data cut power during each save/migration phase, repeatedly | Valid prior or completed transaction; no duplicates/resurrected assets or silent format |
| Capacity | Fill expendable filesystem to reserve; export and delete | Writes reject safely, export works, deletion recovers space; no automatic eviction |
| Reminder timing | Use low SOC, one-minute charging check, scheduled check, stale record, retirement | Correct web/terminal due state; every due asset reachable across pages |
| Speaker | Test normal/zero/max 60 volume, mute, quiet boundary, snooze and codec failure | Short gentle cue, no clipping/pops, cooldown, controls and data survive reboot; UI stays responsive |
| Blue | Perform valid care twice in one day, next day and beyond 40 points; pet repeatedly | Correct once-per-action reward, cap/streak, durable unlocks; petting adds no points |
| Resource stability | Repeat AP start/stop 20 times; scan/export large data while cues play; log heap/stacks | No resets/leaks/deadlocks; adequate main/HTTP/audio stacks and LVGL pool |
| Upgrade | Export, segmented-upgrade old app, restart repeatedly, delete migrated asset | Stable IDs and old history preserved; removed legacy asset does not return |

## References

Inspected upstream `demo/cat-themed-pomodoro-timer` at
`4086b9e8e9e0eca2b547a83560cebb4e3b40059a` and `demo/blufi-provisioning` at
`9c039cc5127f22072afa83bedb7fa3d8efe635ad`, without reusing their UI.
The fork has no matching demo branches. Also used the
[SoftAP resource budget](../../reference/phoenixzhc/softap-provisioning-and-resource-budget.md),
[hardware guide](../../hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md) and
[AI guide](../../development/ai-guide.md).
