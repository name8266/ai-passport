<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# WorldCam for FoloToy AI Passport

A Chinese world-map browser for public camera snapshots on the ESP32-C3
Passport. This feature branch contains the device application, a local image
gateway, and a phone browser connectivity checker.

The catalogue has 825 public camera records across 72 countries and territories.
The separate 195-country capital registry currently has public sources for 50
capitals and decoded images for 49. The requirement for a working camera in every
capital remains incomplete; missing sources are explicitly marked.

## Use the device

| Action | Map | Image viewer |
| --- | --- | --- |
| Previous/next | Select a location | Change location |
| OK | Open the image | Refresh |
| Hold OK | Network setup | Return to map |
| Hold up | Random tour | Random tour |
| Hold down | All locations / capitals | Toggle fullscreen |

Images are snapshots or publisher previews, refreshed every 60 seconds when
viewing. Source capture intervals vary. Fetch time is not capture time.
Fullscreen preserves image proportions with black bars where needed. Native
screens show location, country and fetch time; publisher attribution and source
links appear in the web companion.

## Run the local gateway

Use Python 3.12+ on a computer in the same trusted LAN as the Passport:

```bash
python3 -m venv .worldcam-venv
.worldcam-venv/bin/pip install -r gateway/requirements.txt
.worldcam-venv/bin/python gateway/server.py --host 0.0.0.0 --port 8787
```

Open `http://<computer-LAN-IP>:8787`. Keep the gateway running. On first boot,
join the device's displayed `WorldCam-XXXX` Wi-Fi, open `http://192.168.4.1`,
and enter a 2.4 GHz Wi-Fi network and the gateway URL. Do not use `localhost` as
the device's gateway address. The app saves settings in its NVS namespace.
Do not expose the unauthenticated local gateway to the public Internet.

## Test image access from a phone

The reusable static checker is in `gateway/mobile-test/dist/`:

```bash
python3 -m http.server 8790 --bind 0.0.0.0 --directory gateway/mobile-test/dist
```

Open `http://<computer-LAN-IP>:8790` on the phone. The page loads camera images
directly from the phone's current network, with quick and full tests, pause,
summary copying and JSON export. The page does not upload the report. Keep it
in the foreground; backgrounding pauses active probes. Browser results do not
establish the physical country, VPN status, image freshness or device operation.

For a desktop read-only check, run `tools/probe_worldcam_network.py`; see the
[full guide](docs/assets/worldcam.md) for direct-access and proxy comparison.

## Build and validation

Activate ESP-IDF 5.5.3, install the gateway requirements, and have Node.js 18+
available for the browser-probe host tests:

```bash
./tools/validate.sh
```

The gate validates the repository and host tests, builds ESP32-C3 firmware and
verifies the merged image at `build/FoloToy-AI-Passport-full.bin` (flash offset
`0x0`). Firmware is a build artifact rather than a tracked repository file.
Flashing requires separate authorization; a full-chip erase is not necessary.

Build and host checks pass. USB flashing, native Chinese rendering, button
gestures, Wi-Fi provisioning, heap peaks and long-running operation remain
unverified on physical hardware. Phone image access is separate from device
acceptance.

See the [WorldCam guide](docs/assets/worldcam.md),
[asset sources and licenses](assets/README.md), and
[upstream product documentation](docs/README.md).
