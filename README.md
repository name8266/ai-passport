<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# WorldCam for FoloToy AI Passport

WorldCam is a Chinese world-map browser for public webcam snapshots on the ESP32-C3 AI Passport. **The Passport is now self-contained at runtime: it needs only 2.4 GHz Wi-Fi with Internet access. No PC, Raspberry Pi, proxy service, or local gateway server is required.**

The firmware embeds the maintained catalogue of **825 public camera records and 1020 map locations** in the device's 8 MB Flash. It connects from the Passport directly to the publisher's HTTPS image URL, streams JPEG bytes through a bounded decoder, converts them to RGB565 on-device, and displays them in LVGL.

The separate 195-country capital registry currently has public sources for 50 capitals and decoded images for 49. Missing sources remain explicitly marked; the firmware does not fabricate coverage.

## Device controls

| Action | Map | Image viewer |
| --- | --- | --- |
| Previous / next | Select a location | Change location |
| OK | Open snapshot | Refresh |
| Hold OK | Wi-Fi setup | Return to map |
| Hold up | Random tour | Random tour |
| Hold down | All locations / capitals | Toggle fullscreen |

Snapshots refresh every 60 seconds while viewing. The displayed fetch state means the Passport retrieved that image; it does not prove the publisher's original capture time.

## First-time setup

1. Power on the Passport.
2. Connect your phone to the displayed `WorldCam-XXXX` hotspot using the on-screen password.
3. Open `http://192.168.4.1`.
4. Enter only the 2.4 GHz Wi-Fi SSID and password.
5. After connection, the setup hotspot closes and WorldCam uses the Passport's own Internet connection.

Settings are kept in the application's NVS namespace. A legacy `gateway` key, if present from an older build, is ignored and removed when network settings are saved.

## Direct-device architecture

```text
Passport Wi-Fi → publisher HTTPS JPEG → streaming decoder → RGB565 → LVGL
```

The catalogue, Chinese names, map coordinates and source URLs are compiled into Flash. HTTPS uses the ESP-IDF certificate bundle. JPEG input is streamed instead of buffering the original image in RAM; the display framebuffer is 192×128 normally or 240×160 fullscreen.

One USAP South Pole source publishes a small metadata file rather than a direct JPEG URL. That resolver also runs on the Passport and then fetches the resolved JPEG directly.

The decoder uses JPEG's discrete 1/1, 1/2, 1/4 and 1/8 scaling. It centers the view and may crop edges when the source aspect ratio differs. Progressive JPEG is not supported by the current decoder; such a source is reported as unavailable instead of crashing the UI.

## Build and validation

Use ESP-IDF 5.5.3:

```bash
./tools/validate.sh
```

The ESP Component Manager resolves `jpeg_roi_decoder ^0.5.3`. Validation checks the embedded catalogue is current, confirms the firmware no longer contains the old gateway API contract, runs host tests, builds the ESP32-C3 image and verifies the merged firmware layout.

The repository still contains `gateway/` as **maintenance/reference tooling and the authoritative source catalogue used to regenerate firmware data**. Running `gateway/server.py` is not part of device operation and is not required to use WorldCam.

## Current limits

- Public webcam URLs can disappear, change certificates, redirect, become region-restricted, or switch image formats.
- Direct availability in mainland China depends on the user's actual network and each publisher; the maintenance probe can measure a specific network but cannot guarantee future availability.
- The current catalogue does not provide a working camera for every national capital.
- USB flashing and long-duration physical-device validation should still be performed before treating this branch as a hardware release.

See [WorldCam engineering notes](docs/assets/worldcam.md) and [asset/source licensing](assets/README.md).
