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
| Watchlist | Select stock or Add | Detail or code wheel | Refresh | Cycle sorting | Settings menu |
| Code wheel | Change digit, wrapping 0–9 | Next digit; sixth queries | Previous digit | — | Watchlist |
| Detail | Move chart cursor | Refresh | Add/remove favorite | Cycle intraday/day/week/month | Detail menu |
| Menu | Select item | Apply/open | — | — | Watchlist |
| Alert editor | Change digit | Next; sixth saves | Previous digit | Cycle alert type/off | Detail menu |
| Phone management | — | — | — | — | Watchlist |

The detail menu controls chart period, MA visibility, alerts and favorites.
The list menu controls sorting, power saving, sound, phone management and
refresh. Manual order follows the phone editor; code order is ascending;
percentage order is descending, with unavailable quotes last. The Add row
always follows the stocks. Adding from the device requires a successful quote.

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
120 seconds when there are no alerts; enabled alerts retain 30-second checks.
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
