[简体中文](pocket-netsec.zh_CN.md) · **English**

# Pocket NetSec for AI Passport

A three-button wireless learning and diagnostics application for the ESP32-C3
Passport (240×320, 8 MB Flash, no PSRAM). It replaces the baseline demo entry
point and UI; board pins and BSP drivers remain the product baseline. The
implementation starts from `name8266/ai-passport` main at
`869d212ac12d6b994604581000bdc3bfdfa39465`. The alternative `ai-passport-next`
default branch contained only a README at selection time.

## Tools and controls

UP/DOWN selects an item; OK opens it. Hold OK returns from details to the list,
then from a tool to Home. The original orange-on-dark instrument interface has
12 tools, paging lists, detail panels, a channel histogram, a live signal chart,
status/footer instructions, and a battery indicator. No hardware-test demo
screens are linked into this application.

| Tool | Behavior and controls |
| --- | --- |
| Wi-Fi scanner | Passive 120 ms/channel scan every six seconds, hidden SSIDs included. List sorted by RSSI; OK opens SSID/BSSID/channel/RSSI/security details, then OK selects that BSSID for signal/EAPOL observation. Selection survives reordering. Details suspend new automatic scans. |
| Channel map | AP counts and adjacent-channel weighted RSSI score. UP/DOWN inspects a channel; OK rescans. This estimates AP overlap, not channel utilization or a physical spectrum. Lowest score is a heuristic, not a guaranteed optimal channel. |
| Signal finder | Select an AP first. Receives its beacons on its channel; 60 one-second RSSI samples from −100 to −20 dBm. Missing samples create chart gaps; OK pauses. |
| Packet monitor | Management/data/control counts, Beacon/Probe Request/Probe Response counters, observed frames per second, rejected frames. OK pauses; UP/DOWN changes and locks channel. Optional 500 ms hopping in Settings. |
| Deauth detector | Receives Deauth/Disassoc only; alerts at a configurable aggregate count per one-second window. Shows counts, transmitter, and unprotected reason code. Bursts and MAC addresses are observations, not attribution or proof of attack. Protected reason bodies are not decoded. |
| BLE scanner | Passive legacy advertising scan, duplicate filter per five-second epoch; bounded device list. OK opens address/address type/RSSI/event type/name/raw AD bytes; OK pages raw bytes and decoded flags/company ID/first 16-bit service UUID/TX power, UP/DOWN selects another device. No pairing, connection, or advertising. |
| LAN inspector | Explicitly connects to the network saved over local USB. Displays IPv4 address, mask, gateway, main DNS, and DHCP-client status. OK retries, DOWN enters service discovery while retaining the connection. Connection attempt times out after 15 seconds; no infinite automatic retries. |
| Service discovery | On a connected LAN, bounded eight-second mDNS DNS-SD browse and SSDP M-SEARCH (multicast TTL 1). PTR/SRV/A records and advertised location metadata, up to 24 records. OK opens details, then refreshes; hold UP returns to LAN. Queries remain `.local`, responses must come from the device's IPv4 subnet. Does not fetch advertised URLs or probe ports. |
| Header captures | OK starts/stops a 64-record RAM ring. Hold UP exports to USB; hold DOWN clears it. Frames retain at most 36 bytes of their 802.11 header, with original length and uptime timestamps. Payloads, keys, EAPOL material, and FCS are excluded. Export stops recording. Wireshark sees intentionally truncated frames. |
| EAPOL observer | Locked-channel passive key-info classification into M1–M4. Optional selected BSSID filter. Shows up to eight BSSID/station pairs, seen roles, and repeats in bounded windows. UP/DOWN changes session; OK resets counters; hold UP removes target filtering. It never stores nonce, MIC, PMK, PTK, password, or the handshake body. Missing frames and encryption can prevent complete observation; seen roles do not prove successful authentication. |
| Scan history | Last 12 summaries: boot identifier, uptime, AP total, strongest RSSI, suggested channel, retained AP counts. OK details; hold DOWN clears persisted history. No SSIDs or BSSIDs are persisted as history. |
| Settings | Language, brightness, locked channel, monitor hopping, anomaly threshold, region, history saving. OK cycles the selected value; changes commit to NVS immediately. Region WORLD defaults to channels 1–11; CN/EU supports 1–13 only when appropriate to the user's location. Signal/EAPOL always stay on one channel. |

LAN → service discovery is accessible with the three buttons. Home otherwise
stops the radio and disconnects LAN to conserve memory/power. Captures remain in
RAM until cleared/rebooted; history and settings survive power loss. History is
saved at most once per minute while scanning and on leaving a scan tool, so a
sudden power loss can discard the latest uncommitted scan summary. A radio/page
switch waits for shutdown acknowledgement before reusing resources; stale
queued events carry a generation identifier and are rejected.

## USB setup and export

Use the native USB Serial/JTAG console at 115200 baud. Do not enable local echo
when entering a network password. Commands end in a newline:

```text
HELP
STATUS
SCAN
WIFI <own-network-ssid>|<password>
CONNECT
FORGET
PAGE <1-12>
TARGET <zero-based-scan-index>
PCAP
CLEAR
HISTORY
```

`WIFI` only saves the network; LAN/CONNECT explicitly attempts connection. The
SSID is up to 32 UTF-8 bytes, password up to 64 bytes (empty for open networks).
The serial delimiter `|` cannot occur in an SSID; passwords may contain it.
Wi-Fi credentials are private local NVS data, stored without Flash encryption.
The application never logs/echoes them, and no credentials are embedded in the
build. FORGET removes only the saved network. NVS initialization errors are
reported without automatically erasing other applications' data.

A client with a hidden password prompt avoids shell history:

```bash
python3 -m pip install pyserial
python3 tools/netsec_serial.py --port /dev/cu.usbmodemXXXX --wifi 'Own Lab'
python3 tools/netsec_serial.py --port /dev/cu.usbmodemXXXX --command CONNECT
python3 tools/netsec_serial.py --port /dev/cu.usbmodemXXXX --output headers.pcap
```

`PCAP_BEGIN <count>`, `PCAP_DATA <hex>`, and `PCAP_END` frame a little-endian PCAP
2.4 stream with DLT_IEEE802_11 (105), snaplen 36, and uptime microseconds rather
than wall-clock time. The client validates lengths/counts and ignores unrelated
console logs. It writes an output only after a complete valid transfer. USB
export does not retrieve or decode network payloads. Do not run two clients on
the same serial port.

## Resource and font contract

- 40 retained Wi-Fi APs, 32 BLE devices, 24 service records, 12 history entries,
  eight EAPOL pairs, 64 capture records, 60 RSSI samples, and a 32-event queue.
  Totals can exceed retained APs; scores/counts use the retained strongest set.
- Wi-Fi and NimBLE are initialized/deinitialized by mode, not run concurrently.
  Callbacks perform bounded counter/copy/queue work; the application worker owns
  networking, persistence, serial parsing, and UI updates. LVGL access outside
  its task takes the BSP lock. No application screen is deleted during navigation.
- LVGL uses a 48 KB static pool. The app starts no audio decoder and no full-screen
  screenshot buffer. The font bitmaps stay in Flash. Packet counters count received
  frames including retries; driver loss and hopping mean they are not all traffic.
  BLE queue overflow is visible in its status line.
- Noto Sans CJK SC Regular, SIL OFL 1.1, lv_font_conv 1.5.3, 14 px, 2bpp,
  uncompressed: ASCII, U+3000–303F, U+4E00–9FEF, U+FF01–FF60. The UI selects
  this font explicitly. Unsupported characters (including emoji/other scripts)
  and invalid UTF-8 become `?`; truncation never cuts a valid UTF-8 code point.
  Placeholder diagnostics remain enabled. Coverage audit checks all Han and
  fixed UI characters, plus a known-missing emoji. Physical readability still
  requires checking the Passport screen.

Regenerate the font from the licensed source font available from
[the official Noto repository](https://github.com/notofonts/noto-cjk/tree/main/Sans/OTF/SimplifiedChinese):

```bash
npm install --no-save lv_font_conv@1.5.3
python3 tools/netsec_font.py --font /path/to/NotoSansCJKsc-Regular.otf \
  --converter ./node_modules/.bin/lv_font_conv
python3 tools/netsec_font.py --audit
```

## Build and flash

Activate ESP-IDF 5.5.3 and run the repository's complete gate:

```bash
source /path/to/esp-idf-v5.5.3/export.sh
./tools/validate.sh
```

The validation gate uses tracked defaults with an isolated configuration and
checks the merged image against the partition table and matching ELF. Generated
firmware/debug bundles remain ignored. To render the actual UI with fixtures on
a desktop after Managed Components are available:

```bash
cmake -S tests/netsec_ui -B build/ui-host
cmake --build build/ui-host -j 8
mkdir -p build/ui-frames
build/ui-host/netsec_ui build/ui-frames
```

This exercises all 13 screens and repeated redraws, checks the LVGL pool, and
writes PPM frames; it does not establish real display/radio/device behavior.
macOS uses `-Wl,-dead_strip` in place of GNU `--gc-sections` for the existing host
runtime tests; use a compiler wrapper for this platform-specific translation.

The partition layout stays NVS `0x9000/0x6000`, PHY `0xF000/0x1000`, factory
`0x10000/0x7F0000`, within 8 MB; no OTA or filesystem is added. For an intentional
complete refresh, flash the validated merged image from **0x0**:

```bash
python -m esptool --chip esp32c3 --port PORT --baud 460800 \
  write_flash 0x0 build/FoloToy-AI-Passport-full.bin
```

The merged file pads the NVS gap and can reset saved settings/network/history.
For a compatible update preserving NVS, use the archived separate bootloader,
partition table, and application images with their verified `flash_args` offsets.
Do not put the application-only image at 0x0. No full-chip erase is required.
See [firmware layout and data policy](engineering/firmware-layout.md).

## Device acceptance still required

Build and host checks are not hardware acceptance. On a real Passport verify:
startup/corners/text/battery, all button paths, Chinese and long SSIDs, repeated
Wi-Fi↔BLE switching, low-memory/error recovery, empty/crowded scans, channel
limits, target signal gaps/pause, ordinary laboratory reconnect EAPOL observation,
legitimate disconnect-frame counts, BLE names/raw data, wrong-password/timeouts,
LAN/service discovery, header-only PCAP in Wireshark, and power-cycle settings/
history. Check minimum heap after many mode changes and flash/power-cycle without
assuming latest uncommitted history survives. No disruptive traffic is needed.

No raw 802.11 transmission, Deauth injection, evil twin, credential collection,
password cracking, port brute-force, or BLE attack capability is implemented.
Safe LAN service discovery and explicit own-network station connection are the
only intentional diagnostic transmissions beyond normal protocol operation.
