<p align="right"><a href="stock-quotes.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# A-share stock board

Branch: `feature/stock-quotes`. Target: FoloToy AI Passport, ESP32-C3,
240 × 320 display, 8 MB Flash, no PSRAM, ESP-IDF 5.5.3.

## Screens and controls

The app starts in its own watchlist screen, not the hardware test menu or
WorldCam UI. It supports up to 12 unique Shanghai, Shenzhen and Beijing
six-digit stock codes. One example, `000001`, appears on first boot.

| Screen | UP / DOWN | OK | Long UP | Long OK |
| --- | --- | --- | --- | --- |
| Watchlist | Select stock or Add row | Open daily candles or code wheel | Refresh quotes | Open Wi-Fi setup |
| Code wheel | Increase / decrease current digit, wrapping 0–9 | Advance digit; sixth digit queries stock | Previous digit | Back to watchlist |
| Detail | Move candle cursor | Refresh quote and candles | Add/remove watchlist entry | Back to watchlist |
| Wi-Fi setup | — | — | — | Back to watchlist |

Code entry defaults to `000001`; the exchange is inferred from the code.
Adding requires a valid returned quote. Entries persist across restart in the
`stocks/watch_v1` NVS blob. Duplicates and more than 12 entries are rejected.
Removing shifts remaining entries and persists the updated list.

The list shows name, code, latest price and percentage change, with red for
rises and green for falls. Detail draws the latest 60 daily, forward-adjusted
OHLC candles and shows the cursor date, open, close, high and low. Its header
uses the current unadjusted quote. These two price bases can differ.

## Networking and freshness

No API key, account, gateway or subscription is required for the implemented
public endpoints:

- Quotes: `https://qt.gtimg.cn/q=sh600519,sz000001` (GBK, batched requests).
- Daily candles: `https://web.ifzq.gtimg.cn/appstock/app/fqkline/get?param=sh600519,day,,,60,qfq` (JSON).

These are Tencent's public website data endpoints, not a contracted API with
an uptime guarantee. Formats, availability or access policies can change.
Refresh is every 30 seconds while viewing the list/detail. A displayed provider
timestamp distinguishes the latest returned trading quote from today's wall
clock. Holidays and closed sessions naturally retain the last trading quote.
Network failures preserve prior successful data with an explicit stale-data
message. Failed symbols never become valid zero-price quotes. Manual refresh
is available. Invalid codes and missing daily data show an error.

HTTPS uses the certificate bundle, normal verification and SNTP clock sync.
The network worker owns bounded HTTP/JSON parsing; button callbacks only queue
input. Generation numbers discard results for pages that have been left.
Candles use LVGL drawing primitives without a full-screen image buffer.

On an unconfigured board, connect to the open `Stocks-XXXX` hotspot and open
`http://192.168.4.1` to enter 2.4 GHz Wi-Fi credentials. The app reuses the existing
WorldCam credential namespace so compatible segmented flashing keeps Wi-Fi.
Its watchlist is in a separate namespace and does not alter WorldCam settings.

## Build and acceptance

Run the complete `./tools/validate.sh` with ESP-IDF 5.5.3 activated. The gate
includes pure-C tests for market inference, decimal parsing, GBK conversion,
wheel behavior, watchlist persistence, quote parsing and OHLC validation,
as well as font coverage and bitmap bounds checks. Font sources use Noto's SIL
OFL 1.1 licenses already included in `assets/fonts/`. Regenerate with
`tools/generate_stock_font.py --converter <lv_font_conv-1.5.3>` after fetching
the pinned source through `tools/fetch_worldcam_font_source.py`.

GitHub Actions builds a downloadable verified merged image on this branch.
A merged flash at `0x0` clears the stored NVS region. To keep Wi-Fi and watchlist,
verify a compatible partition table and flash verified bootloader, table and app
components at their recorded offsets instead. Never put an app-only image at
`0x0`.

Physical acceptance must cover screen/name rendering, digit wrapping and back,
stock lookup, adding/deleting/reboot persistence, list navigation/refresh,
candle cursor and refresh, Wi-Fi provisioning, disconnection and stale-data
recovery. A host test or a successful build is not device acceptance.
