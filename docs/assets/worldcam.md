<p align="right"><a href="worldcam.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# WorldCam for AI Passport

WorldCam is a Chinese world-map webcam browser for the ESP32-C3 FoloToy AI
Passport. It replaces the hardware-test launcher with its own map, snapshot,
fullscreen and network-setup screens. A local Python gateway retrieves public
publisher images; the web companion offers keyword search and the same map.

## Catalogue and capital coverage

The maintained release catalogue contains **825 public webcam records across 72
countries and territories**. The source audit decoded **824 of 825 images** at
check time. This verifies public URL access and image format, not original
capture freshness or perpetual camera availability.

A separate registry represents the principal capitals of **195 countries**
(the 193 UN members plus the Holy See and Palestine). Public listings were
found for **50 capitals**; images decoded successfully for **49**. **145 capitals
have no verified source in the searched directories**, and one listed capital
source was unavailable. The request for a working camera in every capital is
therefore **not fulfilled**. A registry point without a source is a geographic
record, not a fabricated camera. It never substitutes a different city, a travel
photo, or a private/exposed camera feed.

`gateway/capital-coverage.json` records country/capital names, search URL,
query time, source matches, unavailable searches and verified camera IDs.
`gateway/source-audit.json` records each image check and its explicit failures.
These records are maintained data, not hidden success claims. The researched
catalogues are Foto-Webcam.eu, SkylineWebcams and Webcam Galore, plus the first
version's official NPS, HKO, GeoNet and USAP sources. Lack of a match in these
catalogues does not establish that no public camera exists anywhere.

Images are periodic snapshots or publishers' public preview images, not
continuous video. A refresh fetches the latest image available from the
publisher. The UI's time is the gateway fetch time; some publishers may retain
an older image. Original source links and attribution remain visible in the web companion.
The native Passport viewer currently shows location, country and fetch time; it
does not show publisher attribution, and fullscreen hides its text overlays.

## Device controls

| Screen | UP / DOWN click | OK click | Hold OK | Hold UP | Hold DOWN |
| --- | --- | --- | --- | --- | --- |
| Map | Previous / next point | View; retry directory if empty | Network setup | Random tour | All points / capitals filter |
| Viewer | Previous / next point | Refresh image | Return to map | Random tour | Toggle fullscreen |
| Setup | No action | No action | Return to map | No action | No action |

Random tour selects an available public-image location and avoids the current
point when another is available. It excludes gaps and audit failures. If an
image subsequently fails, it tries up to three additional random locations.
Normal viewing refreshes every 60 seconds and reports failure explicitly.
Fullscreen removes the title, map, battery and help chrome; the image uses the
whole display width with black letterboxing to preserve aspect ratio.

The device UI is Chinese. Geographic names use maintained Chinese names where
available; some publisher-specific or untranslated place names retain Latin
text. Fixed labels and dynamic names use the same verified 16px font subset.
The built font covers 771 actual code points, including Chinese and extended
Latin. Physical appearance remains a separate board check.

## Run the gateway

Use Python 3.12 or later on a computer on the same trusted LAN as Passport:

```bash
python3 -m venv .worldcam-venv
.worldcam-venv/bin/pip install -r gateway/requirements.txt
.worldcam-venv/bin/python gateway/server.py --host 0.0.0.0 --port 8787
```

On Windows use `.worldcam-venv\Scripts\python.exe` and
`.worldcam-venv\Scripts\pip.exe`. Open `http://<computer-LAN-IP>:8787`.
The default bind address is loopback; `--host 0.0.0.0` enables device access.
Keep the gateway running while using Passport. Permit port 8787 through the
host firewall only for the trusted local network if needed. Do not expose this
unauthenticated local HTTP service directly to the Internet.

The companion has a world map, previous/next buttons, Chinese/English search,
region filters, a capitals-only filter, random tour, refresh, fullscreen,
attribution and source links. Its map also accepts arrow keys, Enter to view,
and R for random tour. Search operates on the maintained directory.

```bash
.worldcam-venv/bin/python gateway/server.py --check-sources --limit 20
```

Omit `--limit` to check all images. Re-auditing and writing maintained check
results uses `tools/audit_worldcam_sources.py`; review source changes and usage
terms first. Only URLs from the maintained camera catalogue can be fetched.
The gateway verifies upstream HTTPS, caps image bytes and pixel counts, bounds
concurrency, and retains at most 64 cached image sets for 60 seconds. Failed
refreshes return errors rather than relabeling old data as current.

## Build and connect Passport

Activate **ESP-IDF 5.5.3**, then run:

```bash
python -m pip install -r gateway/requirements.txt
./tools/validate.sh
```

The gate checks the repository, host logic, font artefact and gateway, then
builds and verifies `build/FoloToy-AI-Passport-full.bin` and the matching debug
archive under `build/firmware/`. There are no compiled Wi-Fi credentials.
Build success does not establish board operation.

1. Flash only after approving the target and exact validated firmware. A merged
   image written at `0x0` replaces firmware and may reset stored NVS settings.
   No full-chip erase is required. See the
   [firmware data policy](../development/engineering/firmware-layout.md#flashing-and-stored-data).
2. On first boot, join the screen's `WorldCam-XXXX` Wi-Fi using its random
   eight-character password. Open `http://192.168.4.1`.
3. Enter your 2.4 GHz network credentials and gateway URL, for example
   `http://192.168.1.10:8787`, without a trailing slash. Use the computer's LAN
   address; `localhost` refers to the Passport itself.
4. Setup closes after successful connection and the point index loads. A failed
   connection retains the setup page for correction. From the map, hold OK to
   reopen setup. The app keeps configuration in its own NVS namespace across
   reboot; overwrite it through setup to change networks. NVS is not encrypted.

BLE is disabled to conserve internal RAM. Disconnections trigger 10-second
reconnect attempts. The setup access point has a fresh random password each
boot. The app never erases NVS automatically on an initialization error.

## Measure mainland-China connectivity

The cloud image audit is not a mainland-China direct-access measurement. The
current cloud outbound IP geolocation reported the United States, and no
mainland test host is attached. Publisher accessibility must be measured from
the network where the gateway will run.

On a mainland-China computer, use this separate read-only probe. It requires
Python 3.10+ and Pillow, but does not require ESP-IDF, a running gateway, or a
Passport. Turn off local VPNs and browser/system proxies before a direct-access
measurement. The probe ignores application proxy settings by default; a router
or transparent VPN can still change the actual route.

```bash
python -m pip install -r gateway/requirements.txt
python tools/probe_worldcam_network.py --vantage-label mainland-local --output WorldCam-mainland-report.json
```

The JSON report records successes and failures per image source, provider
counts and capital coverage. It does not change the maintained catalogue. A
successful result verifies image access and decoding at test time, not live
capture freshness. Physical network origin remains unverified in the report;
the label describes the operator's test context. For a comparison with a
configured application proxy, run again with `--use-env-proxy` and a different
output filename. A direct failure alone does not prove blocking: timeouts,
source downtime, geofencing and rate limits can also cause failures.

## Location and frame protocols

The device fetches `/api/index` rather than parsing all metadata at once.
A maximum of 2048 points requires at most 16 KiB of index memory.

| Offset | Size | Index field |
| --- | --- | --- |
| 0 | 4 | ASCII `WCIX` |
| 4 | 2 | Point count, little endian |
| 6 | 2 | Record size 8, little endian |
| 8 onward | 8 each | Index uint16, latitude int16, longitude int16, flags uint8, reserved zero uint8 |

Coordinates use hundredths of a degree. Flag 1 means verified image availability, flag 2
means capital, and flag 4 means a listed public camera (manual retries are
allowed even when its audit failed). `/api/location/<index>` returns bounded UTF-8 names and metadata;
`/api/frame-location/<index>` resolves the selected point's maintained camera.
A point without a camera returns an error. Index order is fixed by
`gateway/locations.json`; update catalogue, locations, map and font together.

| Offset | Size | Frame field |
| --- | --- | --- |
| 0 | 4 | ASCII `WCAM` |
| 4 | 2 | Width, little endian |
| 6 | 2 | Height, little endian |
| 8 | 4 | Gateway fetch Unix time, little endian |
| 12 onward | width x height x 2 | Row-major little-endian RGB565 |

Normal frames are 192 x 128 (49164 bytes including header). `?full=1` returns
240 x 160 (76812 bytes). The device accepts only these exact sizes, rejecting
malformed headers, coordinates, UTF-8 and truncated downloads. It releases the
old frame before requesting the next; LVGL updates and cache invalidation occur
under the BSP lock. Button callbacks enqueue events only. A generation number
discards responses for points that are no longer selected.

The static simplified world-map asset lives in Flash. Provider coordinates are
used for Foto-Webcam entries; Skyline entries use matched GeoNames city centres.
Other entries specify their precision in metadata. Markers are approximate
locations, not navigation-grade camera mounting positions.

## Assets and reproducibility

See the [asset index](../../assets/README.md) for font and geography sources and
licenses. To regenerate after changing names or coordinates:

```bash
python -m pip install fonttools
npm install --prefix .toolchain/fonts lv_font_conv@1.5.3
python tools/generate_worldcam_font.py --converter .toolchain/fonts/node_modules/.bin/lv_font_conv
python tools/generate_worldcam_map.py
```

The generated font's cmap and bitmap bounds are checked against every maintained
name and actual UI string, including a known missing-glyph negative case.
Place source changes and publisher permission/attribution information in the
maintained catalogue; do not add private or login-bypassing URLs.

## Remaining acceptance checks

Host tests cover map projection, index parsing, filtering and scale, capital
coverage integrity, random selection exclusion, UTF-8 validation, both frame
sizes/colors, missing-camera and upstream-failure responses, and actual font
coverage/bitmap bounds. Browser checks cover map selection, search, filters,
random tour, fullscreen and image retrieval. Firmware checks compile against the
pinned ESP-IDF/LVGL and verify merged-image layout.

Physical board checks remain: Chinese rendering in every state; rounded screen
clipping; map marker placement; all button gestures; fullscreen colors and
aspect ratio; random retry; AP setup with wrong-password correction; Wi-Fi
reconnection; peak heap; long refresh sessions; reboot persistence; and selecting
other points during downloads. No USB flashing or device acceptance is implied.
