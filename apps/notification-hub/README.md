# Passport Notification Hub

[简体中文](README.zh_CN.md) · **English**

An independent, **notification-only**, iPhone companion firmware for the
FoloToy/Passport ESP32-C3. It is not an iOS app, and cannot read every iOS
application's private data. It consumes only notifications that iOS exposes
to an authenticated BLE accessory through [Apple ANCS](https://developer.apple.com/library/archive/documentation/CoreBluetooth/Reference/AppleNotificationCenterServiceSpecification/Specification/Specification.html).

It is based on `main`, not the unrelated Passport Nav branch. Nothing has
been added to the existing demo firmware or other user branches.

## Supported
- Apple ANCS new, modified, removed notification events.
- Read-only ANCS notification attributes: app identifier, title, body.
- Eight recent cards in RAM, UP/DOWN navigation, OK clears local cards.
- Explicit pairing via a fresh six-digit passkey shown on Passport.
- Bond keys persisted using ESP-IDF NVS; notification content never stored
  to Flash, sent to the cloud, or logged.
- Three-button UI and a 240x320 custom dark LVGL view, battery status.
- Chinese rendering using LVGL's built-in Source Han Sans SC 16 CJK **subset**.
  This subset is not sufficient for arbitrary Chinese glyphs or emoji; missing
  characters should remain visible as placeholders, not silently discarded.

No navigation, GPS, music, Wi-Fi, Internet access, microphone or third-party
iPhone app is included.

## Compile

ESP-IDF v5.5.3 is required:

```bash
source /path/to/esp-idf-v5.5.3/export.sh
cd apps/notification-hub/firmware
idf.py set-target esp32c3
idf.py build
idf.py merge-bin -o notification-hub-full.bin --format raw
```

Protocol parser host test from repository root:

```bash
cc -std=c11 -Wall -Wextra -Werror \
  apps/notification-hub/tests/test_hub_protocol.c \
  apps/notification-hub/firmware/main/hub_protocol.c \
  -o /tmp/hub_protocol && /tmp/hub_protocol
```

## Pairing / notifications
1. Flash the build matching your board, turn Passport on, and open iPhone
   **Settings → Bluetooth**.
2. Find **Notify Hub**. Select it and enter the six-digit Passport passkey.
3. Permit system-notification sharing if iOS offers that switch or prompt.
   Keep Bluetooth enabled and allow the original app's notification previews.
4. Trigger a test notification. Passport displays it, and UP/DOWN browses
   recent entries. OK clears only the local volatile cache.

The device solicits ANCS using BLE advertising AD type 0x15; pairing and
permission behavior are subject to iOS version and device testing. Some
notification contents may be hidden by Focus, notification-preview privacy,
app settings, or the app not posting a system notification.

## Privacy and security
A new passkey is generated per device boot; bonding requests LE Secure
Connections with MITM. Numeric-comparison requests are rejected rather
than automatically accepted. Incomplete security setup is not marked ready.
Connection failure or disconnect clears all buffered notification content
from the device's RAM. No phone notification dismissal, reply, or remote
actions are implemented.

**Known prototype limitations**: ANCS service can appear/disappear; real
hardware must verify iOS pairing, reconnection, permission, app attributes,
correct service discovery, long fragmented text, and privacy settings. An
internal ANCS attribute response may get stuck if iOS never responds; robust
timeouts/queueing are a planned improvement.

Merged firmware at address 0x0 includes partitions and bootloader. Flashing
at 0x0 can overwrite NVS settings and unrelated data. Do not erase the device
or overwrite active firmware without checking partition layout and deciding
whether saved data must be preserved. Never confuse an ESP-IDF build success
with a successful iPhone/Passport functional test.

## Upstream API reference
- Apple ANCS specification (link above)
- ESP-IDF 5.5.3 Bluedroid ANCS example (illustrative reference)
