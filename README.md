<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Starmap Tarot for FoloToy AI Passport

Starmap Tarot is a fully offline Simplified Chinese tarot application for the
FoloToy AI Passport. It ships the complete 78-card 1909 Rider–Waite–Smith deck,
local interpretations, four reading modes, persistent history, favorites, and
device settings in one ESP32-C3 firmware image. No account, network connection,
cloud API, or user-data upload is required.

## Features

- Daily guidance, single-card reading, three-card spread, and Celtic Cross.
- Upright and reversed cards with position-aware local interpretations.
- Full 78-card visual library using embedded RGB565 artwork.
- Reading history that can reopen every saved card and its interpretation, with favorite protection and removal of non-favorite records.
- Persistent reversal, sound, brightness, history, and favorite settings in NVS.
- Non-blocking audio feedback, periodically refreshed battery display, automatic dimming, and screen off.
- Wake-only first button interaction after screen-off, with ES8311 software suspend while the display is off.
- Confirmed cleanup for non-favorite history; favorite saturation is surfaced instead of silently dropping a reading.
- Purpose-built navy-and-gold LVGL interface with a compact two-column home screen and embedded Simplified Chinese fonts.

## Controls

| Control | Action |
| --- | --- |
| Up / Down | Move through menus, cards, history records, and cards inside a saved reading |
| OK | Enter, reveal, open interpretations; on a saved interpretation, toggle that reading's favorite state |
| Long press OK | Return one level or home; core browsing screens keep this return affordance visible |

Complete all cards in a reading to save it automatically. In History, press OK
to reopen a saved reading, use Up/Down to revisit each card, then press OK again
to read its interpretation; the interpretation screen can toggle the reading's
favorite state. The cleanup command in Settings
asks for confirmation, keeps favorites, and removes other history records. If
all 32 history slots are favorites, a new completed reading remains visible but
is explicitly reported as unsaved until the user frees a history slot.

After automatic screen-off, the first function-button interaction only wakes the
display; it does not also activate the selected UI action.

## Build

The target is ESP32-C3 with 8 MB Flash and no PSRAM. ESP-IDF 5.5.3 is required.

```bash
source /path/to/esp-idf-v5.5.3/export.sh
idf.py build
```

Run the repository's complete validation and merged-image packaging gate before
delivery:

```bash
./tools/validate.sh
```

The gate creates `build/FoloToy-AI-Passport-full.bin`, which is flashed at
offset `0x0`. Data retention depends on the update path:

| Operation | Tarot history and settings |
| --- | --- |
| Normal power-off or restart | Retained in NVS |
| Same-layout segmented flash using the generated `flash_args` | Retained; the listed bootloader, partition-table, and app writes do not write the NVS partition at `0x9000` |
| App-only write at `0x10000`, with a matching bootloader and partition table | Retained |
| Merged `FoloToy-AI-Passport-full.bin` write at `0x0` | Erased/overwritten; the merged file spans the NVS address range |

This partition table has no OTA app slots, so segmented or app-only flashing is
a development update path, not an OTA mechanism. Use the full image for a first
installation or intentional factory reset. Building successfully is not a
substitute for testing the display, buttons, audio, battery reporting,
persistence, wake behavior, and long-running behavior on real hardware.

## Assets and licensing

The card artwork is derived from the public-domain 1909 Rider–Waite–Smith deck.
Its pinned source revision, license note, and per-file hashes are recorded in
[`assets/tarot/source-manifest.json`](assets/tarot/source-manifest.json). The
Simplified Chinese font subset is derived from Noto Sans SC under the SIL Open
Font License; see [`assets/fonts/`](assets/fonts/).

Tarot readings are provided for entertainment and self-reflection only. They do
not replace medical, legal, financial, or mental-health professional advice.


## Community release readiness

This branch is structured as a community-facing application rather than a hardware-test demo. Before submission, use a 3:4 cover that shows the real 240×320 interface, run `./tools/validate.sh`, and verify the resulting merged firmware on an actual AI Passport. A successful host/CI build is evidence of software reproducibility, not a substitute for checking the LCD, Chinese glyphs, buttons, audio, battery reporting, persistence, wake behavior, and extended runtime on hardware.

Suggested community title: **Starmap Tarot · Offline 78-Card Reader**.
