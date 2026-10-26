<p align="right"><a href="stock-quotes.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# A-share pocket terminal

Branch: `feature/stock-quotes`. FoloToy AI Passport, ESP32-C3, 240 × 320,
8 MB Flash, no PSRAM, ESP-IDF 5.5.3.

## Screens and controls

The independent dark interface uses rounded navy cards, a blue selection,
large prices, red rises and green falls. Full Chinese names use a 16px font;
metadata and hints use a separate 12px Chinese subset. Up to 12 unique
Shanghai, Shenzhen and Beijing six-digit codes are supported. First boot
includes `000001`. Existing stock-app watchlists migrate automatically.

| Screen | UP / DOWN | OK | Long UP | Long DOWN | Long OK |
| --- | --- | --- | --- | --- | --- |
| Watchlist | Scroll/select stock or final Add row | Stock detail; Add opens code wheel | Refresh | Cycle sorting | Global settings |
| Code wheel | Change digit, wrapping 0–9 | Next digit; sixth queries | Previous digit | — | Watchlist |
| Detail | Move chart cursor | Refresh | Add/remove favorite | Cycle intraday/day/week/month | Stock menu (can return to list) |
| Menu | Select item | Apply/open | — | — | Watchlist |
| Alert editor | Change digit | Next; sixth saves | Previous digit | Cycle alert type/off | Detail menu |
| Phone management | — | — | — | — | Watchlist |

The stock menu controls chart period, MA visibility, alerts and favorites, and includes an explicit return-to-watchlist action.
Hold OK on the watchlist to open global settings for sorting, power saving, sound, 30/60/120/300-second quote intervals, phone management, refresh and Add/query. Manual order follows the phone editor; code order is ascending;
percentage order is descending, with unavailable quotes last. The final “+ Add favorite” row is always present: press OK to open the six-digit code wheel, confirm the final digit, then hold UP in the detail view to save the stock. The app returns to the Add row, where OK starts another entry. At 12 favorites it prompts you to remove one first. Duplicate codes stay in the wheel with a prompt to choose another; Add/query in global settings can still inspect an existing stock. A valid six-digit code can be saved offline; the list shows the code and a waiting-for-quotes state until prices and intraday charts arrive.

## Three-chart watchlist

With at least three favorites, the 240 × 320 screen shows three complete
intraday cards simultaneously. Each has a name, price, percentage change,
code, quote time and a small full-session chart with a previous-close reference.
UP/DOWN moves selection and scrolls at the viewport edge with an explicit ease-out
animation: 144 ms for a full row, 120–168 ms when retargeted. Rapid presses
retarget the animation; longer moves continue in bounded short segments without
blank cards. First/last wrap and page returns
snap directly. OK retains the full detail charts. The Add row follows all stocks: with at least three favorites, the first three
cards remain visible together and the Add row appears only when scrolling down.

The five reusable cards are independent of the twelve-stock limit. Each of
twelve RAM-only caches keeps at most 61 prices (the latest sample in each
four-minute bucket); this overview is deliberately less detailed than the
242-point detail chart. Five 200 × 24 I1 rasters use 3,040 static bytes including
palettes. Rendering decodes one scanline at a time, enforced by a compile-time
check; no full ARGB image or per-segment draw queue is allocated for mini charts.
The network result overlays mutually exclusive candle and minute arrays.

Batch quotes refresh first. One background minute request at a time prioritizes
the visible three, then neighbors and the rest. Each result is delivered before
the next request; navigation never waits for HTTP. Attempts per stock are limited
to once per 30 seconds, including failures. Quotes and charts are not guaranteed
to share the same update instant. Loading/unavailable placeholders replace missing
charts; a failure retains a same-date chart dimmed. Different code/date data
never share a chart. A quote timestamp prefixed with * means that stock did not
receive a quote in the latest refresh, or that the device is offline. Its chart
is dimmed. When only some stocks refresh, the list shows a partial state while
the updated stocks keep their normal colors.
No synthetic line is substituted when a source fails. Charts reload after reboot.
If a detail chart fails, successfully refreshed quotes remain current; the chart shows its own error.
Opening a favorite's detail chart reuses the current batch quote instead of
requesting the same stock quote again; a missing batch quote is fetched separately.

The list emphasizes large prices, red/green curves, subdued previous-close lines,
percentage badges sized to their text and a blue selection rail. A thin scrollbar and position counter
show progress through the watchlist. Unchanged text, styles and chart rasters are
reused; idle list updates do not issue display flushes. Scroll completion only
notifies the UI task, preserving the LVGL/state lock order.

## Charts and alerts

Intraday uses trading-session prices, a previous-close reference and volume
bars. Only 09:30–11:30 and 13:00–15:00 records are accepted; after-hours records
are excluded. The date must match the current quote so that previous close
has the correct reference. Cumulative source volume is converted to interval
volume for the chart; the cursor shows cumulative volume.

Daily, weekly and monthly charts each load the latest 60 forward-adjusted
OHLC/volume records. Forty candles are visible at once; UP/DOWN scrolls the
cursor across all 60. MA5, MA10 and MA20 use trailing closes, drawn in gold,
blue and violet. Missing history leaves an MA absent. The price header uses
the current unadjusted quote; candle prices can differ because of adjustment.
Volume is in source-provided lots (hands).

Each favorite can have one alert: price at/above, price at/below, percentage
rise at/above, or percentage fall at/above. Price thresholds support up to
9,999.99 CNY; percentage thresholds are positive magnitudes up to 100%.
On the device, six threshold digits represent cents or hundredths of a
percent; the editor shows the interpreted value. Long DOWN cycles the type,
including Off. A screen notice lasts 15 seconds or until a key is pressed;
a short tone plays when sound is enabled. An alert wakes the backlight.

An alert fires when a successful network quote meets its condition, at most
once per provider trading date. Its date latch is persisted across reboot.
Cached quotes do not fire alerts. Changing its threshold/type rearms it;
resaving the same alert preserves its latch. This is a condition alert, not
an intratick crossing detector: checks run every 30 seconds, including while
viewing another stock or editing settings. Holidays retain the provider's
last trading date. Alerts require the device to remain powered and connected.

## Phone management and Wi-Fi

An owner-only build can embed a default Wi-Fi network. On boot, valid saved
credentials take precedence; otherwise the app immediately tries the embedded
network without requiring hotspot setup. The built-in defaults are not written
to NVS. Opening Phone management still allows a network change; saving there
overrides the built-in defaults. If neither source is available, setup uses the
existing open hotspot. A failed built-in connection can be corrected through
the same menu.
If a saved network remains unavailable through three 10-second retries, the app
tries the embedded default without deleting the saved credentials.

To make this private build, set `STOCK_WIFI_CREDENTIALS_FILE` to an absolute path
outside the repository containing a UTF-8 JSON object with only `ssid` and
`password` string fields. On POSIX, the file must have mode `0600`. SSIDs contain
1–32 UTF-8 bytes; passwords are empty for an open network, or 8–63 UTF-8 bytes.
Every build uses the current value, generating the header only inside
the build directory. Missing or invalid files fail the build instead
of silently omitting the defaults. Leaving the variable unset makes a public
build with empty defaults. Credentials never appear in compiler arguments or
application logs, and must never be put in tracked defaults or source files.
Reusing a build directory after changing or unsetting the variable refreshes
the header automatically. Private BIN/ELF artifacts contain these credentials: keep them outside the
community submission materials and rebuild without the variable for release.

Use the list menu's Phone management entry. On an unconfigured board, or
when that entry opens the hotspot, connect to open `Stocks-XXXX` and browse
`http://192.168.4.1`. No hotspot password is required. The screen also shows
the LAN address; after connecting to Wi-Fi, a phone on the same network can
open that address. Wi-Fi credentials are saved through the configuration
form and never returned by the state API.

The responsive page displays quotes and source timestamps, accepts up to
12 codes separated by spaces/commas/newlines, manages alerts, sorting, sound
and power saving, and configures 2.4 GHz Wi-Fi. Reorder the code lines for
manual order. Save submits one validated configuration to the UI task;
invalid/duplicate codes, duplicate alerts, out-of-range values and alerts
for non-favorites are rejected. A 202 response means queued; subsequent
page status reports persistence failures. Quote display polls every 15 seconds.

The app reuses WorldCam's credential namespace. The atomic `stocks/board_v2`
blob stores watchlist, preferences and alert latches; `stocks/quote_cache`
stores the last successful quotes. Cache writes are throttled to five minutes,
with the first successful refresh and watchlist changes saved immediately.
Reboot displays matching cached names/prices/timestamps until fresh data
arrives. Charts are fetched again and are not persisted. Errors preserve
previous data and explicitly mark it as cached/stale.

After 60 seconds without input, power saving dims the backlight to 20%.
A key restores brightness and performs its normal action. Idle refresh becomes
at least 120 seconds when there are no alerts. Quote intervals can be set to
30, 60, 120 or 300 seconds (default 30); enabled alerts retain 30-second checks.
Existing saved settings migrate with the new interval defaulting to 30 seconds.
Wi-Fi uses modem power saving. The app does not deep-sleep during monitoring.

## Data and validation

No key, account or subscription is used for these implemented endpoints:

- Quotes: `https://qt.gtimg.cn/q=sh600519,sz000001` (batched GBK).
- Candles: `https://web.ifzq.gtimg.cn/appstock/app/fqkline/get?param=sh600519,day,,,60,qfq`; replace `day` with `week`/`month`.
- Intraday: `https://web.ifzq.gtimg.cn/appstock/app/minute/query?code=sh600519`.

These public Tencent website endpoints have no contracted uptime guarantee;
formats and access policies can change. Provider timestamps remain visible.
HTTPS uses the certificate bundle, normal verification and SNTP synchronization.
Network parsing is bounded; buttons only queue input. State is serialized by
the UI task and a snapshot mutex; LVGL reads/writes use the BSP lock.
Generation numbers reject obsolete jobs. Chart drawing is bounded and uses
LVGL primitives without a full-screen buffer. Sound runs in its own task. Each alert explicitly wakes the codec, uses the board demonstration's output level, and queues a silent DMA tail before sleeping. Host tests cover first and repeated playback, tail buffering and failure cleanup.

Run the complete `./tools/validate.sh` with ESP-IDF 5.5.3 activated. Pure C tests
cover market/code inference, GBK names, prices, wheel, watchlists, OHLC,
volume, trading sessions, MA, ordering, alerts and refresh strategy. The
firmware gate also runs host tests against ESP-IDF's pinned cJSON and the
production network/web code, covering all periods, session filtering,
date/volume validation, TLS configuration, response bounds, configuration
validation, chunked request reads and queue failures. Font tests check actual
glyph coverage and bitmap bounds. Regenerate with
`tools/generate_stock_font.py --converter <lv_font_conv-1.5.3>` after fetching
the pinned source through `tools/fetch_worldcam_font_source.py`.

GitHub Actions builds a verified downloadable merged image. A merged flash
at `0x0` clears the NVS region. To preserve settings, verify a compatible
partition table and flash verified bootloader/table/app segments at their
recorded offsets. Never place an app-only image at `0x0`.

Physical acceptance must cover Chinese rendering, all chart periods/cursor,
MA/volume, alerts and sound, threshold wheel, phone configuration, sorting,
reboot/cache/latch persistence, dim/wake, disconnect and recovery. Build and
host results do not establish these physical checks.

## Memory budget

Only the network worker owns an 18,001-byte response arena reserved in BSS.
HTTPS reads directly into it; after TLS cleanup, a bounded JSON view parser
validates the entire document and copies only OHLC/volume or minute records.
It allocates no response buffer or JSON tree on the heap, including repeated
period changes. JSON nesting is limited to 24 and oversized/incomplete responses
are rejected. The network task uses an 8 KB stack, based on observed stack use;
logs include its remaining stack, free/minimum heap and largest contiguous block.
Dynamic TLS record buffers retain a 16 KB incoming-record limit and certificate
verification. A compile-time host-test guard rejects heap allocation in the
response/parser module. Hardware memory margins still need device verification.

## Maintenance checkpoint — 2026-10-23

The three-chart list, fixed cache/raster memory and animated virtualization are
implemented. Complete gate and archive verification passed. The production parser
accepted 242 current intraday records each for 000001, 600519 and 300750; each
produced 61 overview samples. A host renderer exercised 2,000 rapid/reversing/wrapping
selections and all existing pages in a 40 KiB LVGL arena (64-bit host peak 31,976
bytes); this does not measure ESP32-C3 frame rate or total runtime RAM.
Full image: `0bca7392b1faeefc628114d82e6d1b61b61f157b463574ddd35d989429277059`.
ELF: `05fd077d2ac85741c994b11de68bdb1cf70255b5dee525e684c2f9ae9a022815`.
Device testing for this increment is pending: scrolling/buttons, simultaneous
network/audio memory, and a twelve-stock long run. Existing settings/layout are
compatible; use the exact verified segments to preserve NVS after flash approval.

## UI checkpoint — 2026-10-24

The three-card overview now uses larger prices, matching chart/change colors,
rounded change badges, a blue selection rail and a position indicator. Its five
reused 200 × 24 one-bit rasters occupy 3,040 static bytes. Scroll-start callbacks
set the actual LVGL animation to 144 ms for one row (120–168 ms when retargeted);
the bounded planner keeps both viewports covered during rapid key presses.
Unchanged labels/styles and list-page visibility no longer invalidate the screen.

The complete gate, sanitizer preview/planner tests and exact debug archive passed.
The production renderer exercised 2,000 rapid/reversing/wrapping selections,
sorting, all pages, unavailable data, offline cache and the empty list. On the same
64-bit host harness, 100 selections transferred 3,453,912 pixels instead of
6,105,800 (about 43% fewer); 100 unchanged redraw requests transferred no pixels.
These are simulator results, not device frame-rate measurements. The 40 KiB host
LVGL arena peaked at 32,176 bytes; ESP32-C3 RAM margins remain unverified.

Full image: `9ff6a302fd7503675ced9833d017c4e086075024e30b0d82ed95117c7bb8d17f`.
ELF: `d2eea7b331f817a08a2c127b0a1ff93a76b0805eae2528ccc55c496b014e9b69`.
Authorized device flash completed on 2026-10-24 using the exact verified
bootloader and application at 0x0 and 0x10000; esptool verified both written hashes.
The device table has the same NVS/PHY offsets, a 0x6F0000-byte factory partition
and an extra radar_world data partition at 0x700000. Its MD5 was validated and
the app fits. The table and all data regions were retained. LCD/backlight, LVGL
and buttons initialized, and the first 180-second log window contained no fatal
error or reboot. The app entered open-hotspot setup mode without saved credentials
in its namespace; no online quote request occurred in this window. Pending:
provisioning, physical three-chart rendering/scrolling, all chart periods,
network plus audio, settings acceptance and a twelve-stock long run. These checks
require user feedback and are not established by the successful flash/startup.
A further 120-second observation still had no network quote requests or fatal
errors. Reconnecting the serial monitor caused one USB_UART_CHIP_RESET; no
firmware-fault reset was observed. Both bounded monitors are now closed.
Community revision 3573 belongs to the preceding three-chart build and remains
pending review; this UI increment has not been committed, pushed or submitted.


## Default Wi-Fi checkpoint — 2026-10-25

Saved/default credential selection and build-only private injection are complete.
Focused ASan/UBSan checks, UTF-8/byte-limit/open-network tests, C-literal round-trip
compilation, private-file/error-output checks, and the complete repository gate
passed. The current validation image deliberately has empty defaults because the
owner has not yet provided a Wi-Fi SSID/password or private configuration file.
Full image: `ce70b5ee99176677a8b1c30796fec0c1d3ff3b864686ef5b70657d7e643f3ed5`.
ELF: `4d64af7de314db4393a11190ad27d1ae2ec560227ffe7b194f90769563de5b6c`.
This is a generic validation build, not the requested personalized firmware.
No flash, reset, Git write or community submission was performed for this change.
The device still has the preceding UI build; obtaining credentials, making the
private build and its on-device connection/quote checks remain pending.

## Reliability checkpoint — 2026-10-25

The build now regenerates its private Wi-Fi header for every build invocation,
using the current environment. Reusing a build directory cannot silently retain
an old network after the private configuration is changed or removed. Unchanged
headers keep their timestamp, so normal incremental compilation remains fast.
If a valid saved network fails through three 10-second retries, the application
tries the embedded network without erasing saved settings. Wi-Fi configuration
API failures are logged without printing network names or passwords.

The complete gate, focused ASan/UBSan Wi-Fi selector test, private/public
configuration-switch tests, firmware layout and archive verification passed.
Generic full image: `885b050f316e1db9a08dab0c5d7204f859ac741217d4d97c47cb64c92c4e1f33`.
Matching ELF: `14833ded0ca316cb1a288679680ab58b00ba5878e02c3228893ed0e0b4b4c837`.
This build has deliberately empty built-in credentials. A read-only check of the
connected device's NVS found no stored SSID/password keys. The owner has not yet
identified which of several local networks is theirs or provided its password.
The requested personalized firmware and automatic-connection hardware test are
pending that information. No new firmware was flashed, committed, pushed or
submitted to the community in this increment.

## Offline watchlist and quote-status checkpoint — 2026-10-25

The code wheel can now save a valid six-digit stock code without a connection.
Cards use the code as their title and show a waiting state until actual quotes
and intraday data arrive; no synthetic chart is shown. A separate detail-chart
failure no longer marks successfully refreshed watchlist quotes as offline.
The complete gate and firmware archive verification passed. The host LVGL
simulator rendered three cards without quotes, exercised rapid twelve-stock
scrolling and checked the empty list without blank cards or a crash. This is
not a physical display or button test.

Generic full image: `d4fcfacbbe474d7eb5acfc84e24d5e6ab1624f077e6d793876b1fa9f4fea80f5`.
Matching ELF: `39419e1d6af49d82323f773d758c083308697a647c6b6767ea09b9b5091661f7`.
The image contains no built-in Wi-Fi credentials. A private build and device
flash await the owner's SSID/password.

## Detail-load efficiency checkpoint — 2026-10-25

Opening a favorite now reuses its valid quote from the batch request and fetches
only the chart. If that stock was missing from the batch, a separate quote
request remains as a fallback; a recovered quote also updates the watchlist.
A focused test confirms no second quote request on the reuse path. The complete
gate and archive verification passed. This generic image still has no owner's
embedded Wi-Fi configuration and awaits a private build and device acceptance.

Generic full image: `e4b6f6b9a3fe823678b470e5d8c63dc0d4a6319b0903c647d99cf989905a0b2c`.
Matching ELF: `838755f6ca792dfa14406d0a04e22688d765833a428bee32fd655d85f1c7b999`.

## Per-stock quote freshness checkpoint — 2026-10-25

The watchlist now distinguishes quotes received in the latest refresh from
cached values per stock. Only failed stocks gain a `*` before their time and a
dimmed intraday chart; the header shows a partial state when results are mixed.
Freshness follows stock codes through sorting and watchlist edits. A disconnect
marks existing quotes as cached while retaining their values. The detail page
uses its own quote's freshness. Host tests, font coverage, a rendered partial
three-card list, the complete gate and archive verification passed. Physical
display and network behavior await the personalized flash.

Generic full image: `fcfd6d55a4e53747fcc36c1bd951a16f86ea997f60e250768af63935ae3f1c45`.
Matching ELF: `24b27ba1f6e6154a3fe933c9ee2f998538a899d0928484c2bb1b108f834a6e1b`.

## Percentage-badge checkpoint — 2026-10-25

The three-chart list now sizes each percentage background to its text, with
six pixels of padding on each side. Short values no longer leave a large empty
area, while longer values such as `+125.00%` remain visible. Host rendering
covered short and long values, partial quotes and twelve-stock scrolling. A
hundred idle redraws transferred no pixels; a hundred selection changes still
transferred 3,453,912 pixels in the simulator. The complete gate and archive
verification passed. Physical display acceptance awaits the private Wi-Fi build.

Generic full image: `3f8ea8a90085f4e7d34c9344ceb38c18889d52f1f3a8e61e49f4dbf3be081ef8`.
Matching ELF: `f3da3100dfac038d580521fbf520d9e004f43dc7cc694ce5c69743e14f3915d5`.
