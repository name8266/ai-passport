# Passport Notification Archive + AI summaries (development-only)

This branch adds an opt-in AI notification digest **hosted entirely on
Passport**. No self-hosted Python, Docker, Mac, NAS or public gateway is needed.

- Passport itself hosts a small HTTP configuration page at
  **http://192.168.4.1/**, accessible only to a client joined to its
  per-device, WPA2-protected \`PassportHub-XXXX\` Wi-Fi access point.
- On the application-group screen, **long DOWN** displays the AP SSID,
  AP password, and \`admin\` web password. These secrets are generated
  and stored in device NVS. The page uses HTTP Basic Auth + CSRF nonce;
  plaintext HTTP is **not** suitable for production-grade security.
- In the web page, configure home Wi-Fi SSID/password, DeepSeek API Key,
  Chat Completions HTTPS endpoint, model, period (15–1440 min),
  excluded apps, and keyword-based redaction. Saving restarts Passport.
- Default AI provider is **DeepSeek**, with the current model name
  \`deepseek-flash\` and endpoint
  \`https://api.deepseek.com/chat/completions\`. Another OpenAI-compatible
  model may be configured in the same admin page.
- **Cloud AI is OFF by default.** When enabled, the Passport's own
  scheduler sends at most three ANCS notification previews per batch
  (up to four batches per session) directly to the selected provider
  over CA-validated HTTPS, and persists its summaries in Flash.
- Processing resumes after network failure; the source notification
  history is never deleted by a summary run. Only after the local summary
  is durably saved does NVS advance the processed-sequence cursor.
- From the group screen, **long UP** opens the latest summary,
  UP/DOWN pages through it, long DOWN triggers an enabled summary run,
  and long OK returns.

**Privacy warning:** current prototype stores home Wi-Fi and AI provider
credentials in unencrypted NVS, alongside plaintext notification and
digest snapshots in Flash. Admin uses an isolated WPA2 hotspot but HTTP,
not end-to-end HTTPS. Device Flash encryption, secure onboarding, key
rotation, hardware privacy controls, and memory stress tests are needed
before daily personal use.

**No compilation, CI build, simulation, HTTPS request, flashing or iPhone
hardware test has been performed in this iteration.** BLE/Wi-Fi/TLS/HTTP
coexistence on an ESP32-C3 without PSRAM may exhaust RAM, and runtime
stability is unverified. This is a third-party ANCS notification-summary
accessory, not Apple Intelligence or access to private iOS chat databases.

> Branch: feature/notification-archive, derived from feature/notification-hub.
> **Do not build or flash in this iteration as requested.** This code has not
> yet undergone compiler, CI, hardware or on-device validation.

## Scope
Standalone notification-only Apple ANCS accessory for ESP32-C3. There is no
navigation, media, Internet access, or private iPhone notification-reader app.

The first ANCS event is written as a metadata-only snapshot. If the requested
ANCS attributes arrive, a second, detailed snapshot is appended (app identifier,
title, and a limited message preview). ANCS Remove never deletes the historical
snapshots. It does **not** prove that a sender recalled a message; ordinary iOS
dismissal can also produce removal.

The device browses the archived app groups and records using its three keys:
UP/DOWN to select, OK to enter, long OK to return. Historical records can be
read while offline.

## Persistent storage
The 8 MB Flash partition layout now reserves factory app 0x10000..0x400000
(4032 KiB) and an archive FATFS partition 0x400000..0x800000 (4 MiB).
FATFS uses ESP-IDF wear levelling. Each committed snapshot occupies 384 bytes.
At most about 10,000 snapshots are feasible, fewer when considering filesystem
overhead; a notification may create more than one snapshot.

The journal is append-only; its in-RAM indexing coalesces temporary source
placeholders with detailed captures when they can be correlated. On reboot
the archive is scanned and reindexed. At capacity **stop writing** rather than
silently overwriting old records. Flash has finite erase endurance. Complete
durability during power failure, notification bursts and corruption is not
guaranteed.

The archive is never auto-formatted if the *entire* data partition is nonblank.
Mount errors and invalid records preserve existing bytes and halt further
writes. Limited UI capacity: at most 96 indexed application groups.
Text limits: 63 bytes app identifier, 95 bytes title, 191 bytes preview body.
An early withdrawal, hidden preview, or missing ANCS attribute response cannot
be reconstructed. Original images, voice, chat database, and app history remain
inaccessible.

## Privacy warning
**The archive stores sensitive notification text in plaintext Flash.** BLE
bonding protects transport, not storage at rest. Someone possessing the board
or reading its memory may obtain saved previews. Device PIN, export/deletion
controls, encryption, key management and secure wipe need additional design
before personal daily use. These captures are not cryptographic proof.

## Firmware migration
The previous firmware has a factory partition covering almost all Flash.
Changing the partition layout requires a review of existing saved data.
Never blindly run erase-flash or a merged offset-0x0 image when retention
matters. Build/test has intentionally been deferred.

Official references:
- https://developer.apple.com/library/archive/documentation/CoreBluetooth/Reference/AppleNotificationCenterServiceSpecification/Specification/Specification.html
- https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/api-reference/storage/fatfs.html
