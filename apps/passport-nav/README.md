# Passport Nav — iPhone + Passport navigation display

[简体中文](README.zh_CN.md) · **English**

An independent ESP32-C3 firmware and iOS 17+ companion app. The iPhone runs
Apple MapKit route search/navigation and sends a 20-byte BLE telemetry packet.
Passport displays the latest valid telemetry, not a rendered map. The project
does not modify the original hardware-test menu or other feature branches.

## Layout
- firmware/: ESP-IDF 5.5.3 standalone project, using the repository BSP.
- ios/: SwiftUI/CoreLocation/MapKit/CoreBluetooth app; XcodeGen spec.
- tests/: host-side protocol unit tests.

## iPhone
Install XcodeGen on a Mac, then:
\`\`\`sh
cd apps/passport-nav/ios
xcodegen generate
open PassportNav.xcodeproj
\`\`\`
Choose your own Team and unique bundle identifier in Xcode and run on a physical
iPhone; BLE and GPS cannot be fully validated with iOS Simulator. Allow
location and Bluetooth, connect to the peripheral named **Passport Nav**, search
a destination, and tap Start. Demo mode sends test data without routing.

No Gaode API key is required in v0.1: Apple MapKit is used. Navigation is an
experimental step-following implementation, not a safety-certified instrument.
Route text classification and off-route recalculation need field testing.

## Firmware
\`\`\`sh
source /path/to/esp-idf-v5.5.3/export.sh
cd apps/passport-nav/firmware
idf.py set-target esp32c3
idf.py build
idf.py merge-bin -o build/passport-nav-full.bin --format raw
\`\`\`
To test the parser without ESP-IDF:
\`\`\`sh
cc -std=c11 -Wall -Wextra -Werror \
  apps/passport-nav/tests/test_nav_packet.c \
  apps/passport-nav/firmware/main/nav_packet.c -o /tmp/navtest && /tmp/navtest
\`\`\`
A merged full image contains bootloader and partition table. Flashing it at
0x0 can overwrite device settings. Do not erase or flash an existing device
without reviewing partition compatibility and data retention. Use segmented
development flashing when preserving NVS.

## BLE wire format
- Service UUID: 8A2FA760-11F0-4FD9-9B94-6E092E60E21A
- Characteristic UUID: 8A2FA760-11F0-4FD9-9B94-6E092E60E21B (write with response)
- 20 bytes, little-endian; [0]=A5, [1]=version 1.
- [2] maneuver: 0 none, 1 straight, 2 left, 3 right, 4 U-turn,
  5 arrive, 6 bear left, 7 bear right.
- [3] flags: bit0 navigating, bit1 GPS available, bit2 rerouting.
- [4..5] meters to next turn; [6..7] speed in 0.1 km/h.
- [8..9] remaining distance in 10-meter units.
- [10..11] remaining seconds; [12] local ETA hour; [13] ETA minute.
- [14..15] sequence number; [16..17] bearing degrees (0..359).
- [18] zero/reserved; [19] XOR of bytes 0..18.

Passport displays stale/no data after 3 seconds without valid packets. It
shows battery percentage when the board battery driver reports it. UP/DOWN
cycles three display modes; OK toggles brightness (100/30%).

## Limitations and security
BLE writes are not authenticated or encrypted in this prototype. Nearby
clients can attempt to connect: do not transmit sensitive information.
A release must implement bonding with an authenticated provisioning flow or
another suitable access-control approach, and verify it on actual hardware.
No offline maps, spoken navigation, camera recording, CarPlay integration, or
cross-app reading of Gaode navigation are included.

Background location and BLE restoration are configured in the iOS app but
reliable locked-screen transmission and reconnection **are unverified**.
MapKit search and directions availability depend on network/region. Display
text on Passport is ASCII, avoiding missing Chinese LVGL glyphs; Chinese
road instructions remain in the iPhone app.

## Validation
GitHub workflow passport-nav.yml runs protocol host tests, ESP-IDF build and
macOS iOS-Simulator compilation. Only a completed green job constitutes a
verified CI build; device testing requires separate hardware verification.
