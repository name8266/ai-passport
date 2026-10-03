<p align="right"><a href="worldcam.zh_CN.md">简体中文</a> · <strong>English</strong></p>\n\n# WorldCam direct-device engineering notes

WorldCam runs directly on the ESP32-C3 AI Passport. A gateway server is no longer a runtime dependency.

## Catalogue

The maintained source data contains 825 camera records and 1020 locations. The firmware generator compiles URL, resolver, coordinates, flags, Chinese location name and Chinese country name into `main/worldcam_catalog.c`. The 195-country capital registry remains a maintenance/audit dataset; it does not imply every capital has a working source.

Regenerate after changing `gateway/cameras.json` or `gateway/locations.json`:

```bash
python3 tools/generate_worldcam_catalog.py
python3 tools/generate_worldcam_catalog.py --check
```

The historical `gateway/` name is retained for maintenance compatibility. It is not required by the device at runtime.

## Network and image path

```text
Wi-Fi station
  → esp_http_client HTTPS
  → ESP-IDF certificate bundle
  → bounded streaming reader
  → jpeg_roi_decoder
  → RGB565 framebuffer
  → LVGL image
```

The compressed JPEG is not stored as a whole in RAM. Input is prefetched in a 2 KiB decoder buffer, decoder work memory is 4 KiB, and output arrives as RGB565 rows. A 16 KiB network worker stack is used because HTTPS/TLS adds deep call stacks.

A per-source read ceiling of 8 MiB bounds bandwidth and parser exposure. Normal output is 192×128; fullscreen is 240×160. The old WCIX/WCAM gateway binary transport is no longer used.

## Wi-Fi provisioning

The device AP and local setup page remain because they are the simplest way to enter Wi-Fi credentials on a three-button device. The setup form stores only SSID/password. After STA connection succeeds, the AP and local HTTP server are stopped. No gateway address is requested or stored.

## Source handling

824 of the 825 maintained camera records use direct HTTPS JPEG URLs in the current dataset. One USAP South Pole entry first returns metadata; `wc_resolve_usap()` validates the advertised filename and builds the corresponding official HTTPS JPEG URL on-device.

The current decoder does not support progressive JPEG. It uses discrete scaling factors and a centered viewport, so aspect-ratio differences can crop source edges. Failures are surfaced to the UI and do not substitute stale images.

## Mainland connectivity measurement

Cloud auditing is not proof of mainland-China availability. To measure a particular network, the optional maintenance probe can still be run from a computer:

```bash
python tools/probe_worldcam_network.py --vantage-label mainland-local --output WorldCam-mainland-report.json
```

That tool is diagnostic only. It is not required for Passport operation.

## Validation status

Host validation covers catalogue generation, selection logic, USAP resolver parsing, UTF-8 and direct-device contract checks. Firmware CI builds against ESP-IDF 5.5.3 and the managed JPEG decoder.

Physical-device items still requiring release acceptance include Wi-Fi provisioning, TLS access from the intended network, heap/stack peaks, JPEG colors and cropping, button gestures, reboot persistence, location switching during a fetch and sustained 60-second refresh operation.
