# Passport Notification Hub — Public 1.0.0 RC2

**Release candidate for customer installation.** Automated host regressions, memory sanitizers, ESP-IDF 5.5.3 firmware build and release hygiene checks are required. Actual iPhone connectivity, power-cut fault injection and endurance testing remain deployment acceptance criteria.

Passport is an ESP32-C3 (8 MB Flash, no PSRAM, 240×320 screen, three buttons) accessory. It consumes Apple's ANCS notification metadata and limited previews, keeps bounded local history, and can generate structured AI digests directly from the device using user-supplied HTTPS credentials. No companion iOS application, NAS or additional gateway is required.

### RC2 memory and lifecycle changes

The ANCS transport now uses ESP-IDF 5.5.3 NimBLE instead of Bluedroid (a conditional Bluedroid source path remains available for rollback). Host regressions and firmware compilation are required; **actual iPhone pairing and notification delivery still require device validation**. Provisioned devices with AI disabled no longer initialize Wi-Fi/Web during startup: long OK enters the on-device settings page and starts administration on demand. Leaving setup with no web requests for two minutes shuts down HTTP and Wi-Fi in a temporary task. First-install/unprovisioned devices keep the setup hotspot available; AI-enabled devices keep network access for automatic summaries.

Sound is disabled by default and its worker starts only when enabled. Heap telemetry records current/minimum internal memory, largest free block and stack watermark. Web requires a 24 KB/8 KB minimum, and TLS 48 KB/20 KB; these are safeguards, not measured reserve claims. If resource thresholds fail, the optional service is deferred rather than risking BLE capture. A device-level stress test is still mandatory.

### Clean customer setup

Download the release artifact, then follow **客户安装说明.md** (the included Chinese installation guide). The package contains **two distinctly named BIN files**: full merged firmware for a freshly erased device, and an application-only update for a compatible existing device. Never erase a device that contains data you want to preserve. Beta 17 and older FATFS layouts may not be compatible with the 512-byte safe wear-levelling layout; **no automatic data migration is provided**.

After flashing, pair the iPhone with **Notify Hub**, allow notification sharing, join the device's `PassportHub-XXXX` setup Wi-Fi and open `http://192.168.4.1/`. Configure the customer's own network and optional AI provider in settings. AI is **disabled by default**. The default DeepSeek endpoint and model are editable provider options, not pre-installed user accounts or API credentials. No sample notifications or example tasks are preloaded on a clean flash.

A digest triggers after three newly received notifications; old tasks are retained deterministically until acknowledged or individually completed (up to five items). Hiding a digest does not clear its context. Periodic retention cannot discard unprocessed notifications; this can fill storage, so warnings must be monitored.

**Known and accepted security limitation:** the setup hotspot remains open and the LAN web interface has no login, as required for this release. Anyone with network access may view notifications and change settings; CSRF is not authentication. Local data and credentials are not guaranteed encrypted at rest. Do not deploy to untrusted networks or highly sensitive environments without additional safeguards.

ANCS does not guarantee delivery of all notification bodies, nor can it recover content not supplied by iOS. AI output is fallible. The production firmware ships with no mock data; sanitizing a previously used device requires an explicitly authorized first-install flash erasure.

See [Public交付验收清单.md](./Public交付验收清单.md) for test gates and exclusions. Refer to the repository root license before redistribution.
