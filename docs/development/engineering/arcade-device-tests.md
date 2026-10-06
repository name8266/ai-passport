<p align="right"><a href="arcade-device-tests.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Odds Arcade Remote Device Tests

The `feature/slot-machine` development configuration enables
`CONFIG_ARCADE_USB_TEST`. Disable it in `sdkconfig.defaults` for a build that
must not accept development commands. This interface is local USB only and
never writes persistent settings or initiates networking.

The LVGL pool is 96 KB. Static views consume approximately 14 KB; the padded
ARGB dice rotation layers reach 74×73 pixels (21,608 bytes per layer), before
draw-task/style overhead and fragmentation. Both the original 24 KB pool and a
64 KB pool failed allocation on the board, the latter during autoplay. Keep the
layer budget when changing dice size or adding transformed objects, and verify
actual peak LVGL usage, largest free block and minimum system heap on the board.

USB commands enter the same input queue as physical button events:

| Command | Action |
| --- | --- |
| `u`, `d`, `o` | UP, DOWN, OK click |
| `U`, `D`, `O` | UP, DOWN, OK hold |
| `?` | Page, selection, animation, autoplay, speed, sound, completed rounds, heap, LVGL free memory, USB task stack watermark, audio readiness, successful PCM writes, audio errors, peak LVGL usage and largest free pool block |

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

The bounded test covers every game at all three speeds, sound toggles, autoplay,
returning during an active animation, and 40 additional enter/play/exit cycles.
It fails on missing state responses, game timeouts, detected runtime exceptions,
queue overflow, and excessive retained memory loss. Logs remain local and must
be reviewed and sanitized before sharing. The test leaves the device at home.

These checks establish execution of real UI, animation and audio code on the
board. They cannot establish panel appearance, audible quality, actual button
contacts, or electrical measurements. Report those separately as unverified.

## Recorded hardware regression

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
