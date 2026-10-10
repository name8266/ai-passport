<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Passport Notification Archive + AI summaries

Standalone ESP32-C3 notification accessory using Apple's public ANCS service.
It saves notification metadata immediately, then app identifiers, titles and
limited previews when iOS provides them. Removing a notification on the phone
never deletes the local history. ANCS removal does not prove a message recall;
hidden previews, images, voice messages and private chat databases cannot be recovered.

## Current public-test checkpoint

Installed version: `notification-public-beta-15`; application SHA256
`d79dfae05ccb41f711b930067a22fb68929ce85388098270ec3964d8b549c81d`.
Application-only flashing preserves network settings, provider credentials and
archive data. Startup opens the combined newest-first notification feed. Each
browse loads one record; the 500-notification host test bounds forward and
backward traversal linearly. Duplicate app/title text is folded to give the
preview more space. Light and dark themes were captured from actual LCD RGB565
submissions; the summary view exposes the long-DOWN run shortcut.

Build and host tests pass. Device remote-button regression passed 33 requests;
settings-theme round trips retained other form values and the stored-key state,
and the device rejoined its existing LAN. The unrelated webcam website is removed from this notification branch;
repository checks and host tests now pass without website-document interference.
Physical switches, current phone-notification delivery, acoustic quality and
extended concurrent phone/API operation remain unverified; this checkpoint is
not final product acceptance. No synthetic notifications were added in this
round, and no model requests were made.

## Remote device controls

`/device` mirrors the current screen text and exposes all three buttons, their
long presses, and a sound preview. Commands use the physical-button queue;
LVGL text is copied under the display lock. The HTTP handler waits for a bounded render acknowledgement before
redirecting to the updated screen text; Refresh also retrieves asynchronous content. This is a text mirror, not an LCD pixel capture.
Multiple open pages share a boot-scoped CSRF token, so opening another page
does not invalidate an existing settings form.

## Setup and controls

The device creates an **open `PassportHub-XXXX` hotspot with no Wi-Fi password**,
as requested. Long OK on the combined notification feed shows the hotspot name and
`http://192.168.4.1/`. Connect to the hotspot and open that address; the
notification dashboard opens directly without an admin login. `/settings`
opens device settings. The open hotspot grants connected clients access to the
dashboard and controls, so keep it nearby and do not expose it to an untrusted
network. Settings forms retain a boot-scoped CSRF nonce, and archive deletion also
requires typing `CLEAR`.
If an archive partition is empty, startup also resets a stale AI cursor before
processing new notifications.

The on-device page configures home WPA2 Wi-Fi, an OpenAI-compatible HTTPS
endpoint, model, API key, interval (15–1440 minutes), batch size (1–8), excluded
app identifiers and keyword filtering. The default endpoint is
`https://api.deepseek.com/chat/completions`; the configurable default model is
`deepseek-flash`. Eight direct board requests passed in the diagnostic build;
other configurations require separate verification. Saving restarts
the device; blank password/key fields retain their previous values.
Wi-Fi credentials and the AI key are stored separately in NVS and are not
changed by retention cleanup or archive erasure. An application-only firmware
update at `0x10000` preserves NVS.

**Cloud AI is off by default.** If explicitly enabled, selected previews go
directly from Passport to the configured provider using CA-verified HTTPS.
No Mac, NAS, Python gateway or Docker service is required. Each run processes
up to four batches; backlog resumes later. Summaries are appended to Flash
before advancing the NVS processing cursor. Failed requests retain source history.

| Screen | Control | Action |
| --- | --- | --- |
| App groups | UP/DOWN, OK | Select an app, open its history |
| Notification centre (startup) | UP/DOWN, OK | Browse all apps newest first, open full text; stop at either end |
| Notification centre | Long UP / long DOWN / long OK | Latest AI summary / app groups / web setup |
| App groups | Long UP / long OK | Latest AI summary / web setup |
| History | UP/DOWN, OK | Select a snapshot, open full text |
| Full text / summary | UP/DOWN | Scroll long content |
| Summary | Long DOWN | Request a run, if AI is enabled |
| Full text | Long OK | Return to the current notification |
| Summary / web setup | Long OK | Return to the newest notification |
| App history | Long OK | Return to app groups |

Pair with `Notify Hub` in iPhone Bluetooth settings and allow notification
sharing. The application uses its own archive, reading, summary and setup pages.
The top-right corner shows battery status when available.

## Persistence and limits

| Partition | Offset | Size |
| --- | --- | --- |
| NVS | `0x9000` | 24 KiB |
| PHY | `0xF000` | 4 KiB |
| Factory firmware | `0x10000` | 4032 KiB |
| Archive FATFS | `0x400000` | 4096 KiB |

The wear-levelled journal uses 384-byte snapshots, with up to 96 indexed app
groups. App identifiers are limited to 63 UTF-8 bytes, titles to 95, previews
to 191. A notification can produce multiple snapshots. Filesystem overhead
and the reserved digest budget limit the journal to at most 9,472 snapshots.
Storage stops writing at capacity; it never silently replaces older records.
The default retention is 30 days. After trusted network time is available,
the archive worker periodically removes timestamped records older than the
selected period and clears the stale summary. Untimestamped legacy records
are preserved rather than guessed or deleted.

A whole-partition blank check gates initial formatting. Nonblank mount failures,
corrupt records and incomplete journal tails stop further writes without
formatting or truncating history. A partial summary tail prevents further
summary appends; the preceding complete summary remains readable. An unavailable archive can be explicitly initialized: long OK opens settings; select archive recovery to open a Chinese
warning; long DOWN confirms erasing only the 4 MiB archive. OK cancels. Healthy
archives cannot be erased through this recovery action. Export is not implemented. Power loss and queue
saturation can still lose uncommitted notifications. UIDs are correlated within
one BLE connection, not across reconnects.

Device controls, statuses, warnings and web settings are in Simplified Chinese.
The bundled Noto Sans SC 16 px font provides 7,540 glyphs, including GB2312
Chinese and the fixed interface. Unsupported characters, including emoji,
are displayed as `?`; original stored text is preserved. See
[font provenance](../../assets/fonts/README.md). Screen rendering still requires
physical acceptance. Credentials, previews and summaries are currently stored in plaintext Flash.

## Review and validation

The October 2026 review fixes the first-boot configuration failure, C string
syntax and SSID buffer compile errors, parser overflow handling, malformed form
acceptance, cross-connection archive correlation, torn-summary alignment,
ANCS pairing/discovery ordering, request timeouts and long-text reading. Large
worker temporaries are kept off the archive/TLS call stacks. Firmware uses the
repository's pinned dependencies, including ESP-IDF 5.5.3 and LVGL 9.5.0.

From the repository root, activate ESP-IDF 5.5.3, then run:

```bash
./tools/validate.sh --all notification-hub
# Individual stages, including when unrelated repository checks fail:
./tools/validate.sh --host
./tools/validate.sh --firmware notification-hub
```

The complete gate includes repository checks, host tests and this application's
firmware. The `worldcam-site/README.md` language/pairing errors predate this
review and are outside the notification application. They currently block the
repository check; run the individual host/build stages to inspect those results.
Host tests exercise ANCS fragmentation/overflow/UTF-8, configuration defaults
and failures, form validation, archive reboot indexing, session isolation and
torn writes, plus the existing BSP and repository tooling tests.

The build outputs `build/notification-hub/FoloToy-AI-Passport-full.bin` and a
matching ELF/MAP bundle at `build/notification-hub/firmware/<sha256>/`.
The shared filename is a tooling convention; the application version is
`notification-public-beta-8`, and the source is this standalone application.
Verify the specific bundle using `python3 tools/archive_firmware.py verify <bundle>`.

The merged image is for offset **`0x0`** and can reset NVS settings. For existing
history, review partition compatibility and use matching segmented images; do
not run `erase-flash`. Migration from the old factory-only layout requires
checking whether old firmware data occupies the archive partition. A nonblank
incompatible archive will intentionally refuse to mount.

**The previous firmware was flashed, but startup failed** with an HCI host
initialization error and an incompatible nonblank archive. This revision starts BLE
before network/archive workers, reduces Wi-Fi RAM use and Bluetooth connection
limits, and logs internal free heap/largest block. The exact previous allocation
failure has not been established; this revision requires new device verification.
Pending checks include iPhone pairing/reconnect, boot, initial
formatting, notification bursts, physical power loss, Flash exhaustion, Chinese
and emoji rendering, web setup, SNTP/provider HTTPS, radio coexistence and
runtime heap/stack margins still require hardware acceptance. A successful
build and host tests are not device validation.

## Resumed-work checkpoint

`notification-archive-zh-2` passed build, merged-image/matching-ELF archive verification,
host tests and glyph coverage checks. The complete repository gate remains
blocked only by the WorldCam documentation errors above. This revision has
been flashed with explicit approval to `/dev/cu.usbmodem101`, application only,
with data hash verification. A 40-second observation showed BLE, hotspot and
web-server startup without panic/watchdog or fixed-glyph lookup errors. The
incompatible old archive was preserved and remains unavailable, so storage
acceptance failed. JTAG captured complete real LCD submission buffers for the home and recovery
pages, with clear Chinese rendering. A Mac scanner observed Notify Hub BLE
advertisements. Camera photographs, physical buttons, iPhone notifications and
HTTPS summaries remain unverified. Debugging ended with the home view restored
and the old archive untouched.
Use the matching application image at `0x10000` to preserve NVS/archive; archive
initialization still needs explicit confirmation on the device. Full-image SHA-256:

```text
67baed56dda7e673c62d50fe43ec1c0057b048d954c4c885e0745c1990e459cb
```

## iOS-inspired interface and sounds

`notification-ios-sound-3` uses a light gray canvas, white rounded cards, blue
selection and 24 px headings. Dynamic notification text retains the 16 px
GB2312 font. Long OK opens sound settings; UP/DOWN selects, OK changes, long OK
returns. Controls include enable/disable, bright/soft/two-note original chimes,
20–60% volume and preview. Defaults: enabled, bright, 30%. Mute suppresses
both automatic alerts and preview. A worker persists preferences in NVS across
reboot; save/play errors remain visible in settings and another change retries.

Only new notifications request sounds. Silent/pre-existing ANCS notifications,
modifications and removals do not; bursts coalesce with a two-second cooldown.
See [Apple ANCS flags](https://developer.apple.com/library/archive/documentation/CoreBluetooth/Reference/AppleNotificationCenterServiceSpecification/Appendix/Appendix.html).
The original sine chimes contain no sampled Apple audio.

Audio and persistence run in a dedicated worker. PCM is generated in chunks;
playback ends by suspending the codec and releasing I2S DMA, retaining shared
I2C. Mute stops further PCM submission; a short queued DMA tail may still play.
With AI disabled, network setup releases the unused TLS worker stack. Web AI
changes still reboot the application. Old archives are never erased automatically.
Build, host and font checks passed; this revision has not been flashed. Actual
UI, audibility, mute cancellation, bursts, audio/radio coexistence and runtime
heap/stack margins remain unverified.

## Temporary device-side AI verification

The `notification-ai-test-4` revision adds a default-off `HUB_DEVICE_TEST`
Kconfig option. Its diagnostic build waits for local JTAG injection of a test
Wi-Fi connection and API settings into RAM. It invokes the same direct HTTPS
engine used by the normal archive worker, with three synthetic notifications
per batch and at most eight batches. Cases include ordinary reminders,
verification-code filtering, and an untrusted instruction embedded in a
notification. Results use the real digest page and notification sound worker.
Synthetic records, summaries and credentials are never saved to NVS or FAT.
The Wi-Fi driver uses RAM storage; normal provisioning remains owned by the
application's existing NVS settings. Bearer scratch storage is cleared after
HTTP cleanup. All tests run in the existing AI worker to serialize requests.

`tools/test_notification_ai_device.py` controls an already authorized, flashed
board through an existing local OpenOCD session. It checks the running ELF
identity before writes, uses a private credential file outside the repository,
limits the run to 480 seconds after network setup, and records counters without
credentials. Configure OpenOCD with RTOS discovery, GDB flash programming and
flash memory mapping disabled, as required by the previously verified local
JTAG setup. Credentials are cleared by the worker on completion or stop; reset
also clears them. The runner does not flash, format or act as an HTTP gateway.

The diagnostic revision `notification-ai-test-6` was flashed with application-only
writes and completed eight direct DeepSeek requests (HTTP 200), taking
2.015–2.554 seconds each. The verification-code batch sent only two records;
other batches sent three. A complete LCD submission-buffer capture shows the
actual returned Chinese digest, not a host-side mock. Test mailbox credentials
were cleared at completion, and no API key was compiled or saved to NVS.

Device testing found and fixed IPv6-first DNS timeouts and RSA-4096 verification
allocation failure. Requests now select IPv4; response allocation waits until
HTTP body receipt, after authenticated TLS. The LCD uses a 16-line DMA buffer
(the BSP default remains 40), LVGL has a 24 KiB pool, archive ingress has eight
slots with its existing overflow indication, and TLS buffers use dynamic
allocation while retaining the full 16 KiB incoming-record limit. The normal
worker reuses request/configuration buffers to avoid a larger static RAM
footprint than the diagnostic build. Certificate and hostname checks remain on.

The diagnostic heap low-water API reported 968 bytes, with at least 5,300 bytes
of AI task stack unused; concurrent phone traffic and long-duration operation
remain unverified. Transient per-request heap reductions matched 208-byte TCP
TIME_WAIT control blocks; after connection expiry and test worker exit, idle
heap recovered to about 44.9 kB. Ten sequential sound previews then retained
44,872 free bytes. Injected button-queue events verified mute and preview logic;
they do not verify physical buttons or acoustic quality. Codec sleep register
checks passed. A debugger inferior-function call during the initial DNS
investigation caused a cache-error reset; subsequent captures/observations use
hardware breakpoints and memory reads, without inferior-function calls.

The existing incompatible nonblank archive is preserved and remains unavailable.
No normal archive-to-AI acceptance is claimed by the RAM-fixture test. The full
repository gate remains blocked by existing WorldCam README language pairing;
separate host tests and firmware/layout/archive verification passed.


## Scheduling hundreds of notifications

`notification-scalable-7` uses a fixed queue of 512 UIDs, approximately 2 KiB.
Queued duplicates coalesce, removal cancels pending requests, and reconnect
clears session state. Only one ANCS detail request runs at a time; hundreds of
bodies are never retained in RAM. Overflow is visibly reported. Unlimited
bursts and power loss before a Flash commit can still lose notifications.
Metadata bursts leave two capture slots reserved for serialized detail replies.

An eight-entry capture queue is separate from a single latest-only browsing
request. Each archive worker iteration drains at most eight captures before
servicing AI and browsing, preventing starvation. Every capture still syncs
individually; throughput claims do not rely on delaying durability. A random
reverse lookup can still scan the journal. A single navigation-position cache
accelerates adjacent browsing in both directions and expires after appends;
a compact 513-slot UID/session fingerprint cache links pending metadata to the
exact Flash slot. Each candidate fingerprint is verified against the full UID
and session read from Flash, so collisions cannot merge unrelated alerts. This
cache covers the 512-entry detail backlog plus one request in flight; it stores
no notification bodies.

AI collects detailed snapshots only, seeks directly to the committed sequence,
and reads sequentially in O(N) across the backlog. The default batch limit is
eight; existing smaller settings remain valid. Each window processes at most
four batches, waiting one minute while backlog remains or at least two minutes
after failure. Compact batches use up to 63 title bytes and 95 body bytes,
truncated at UTF-8 boundaries; archival lengths are unchanged. Sensitive-word
classification runs on full archival text before truncation. Successful digest
storage precedes checkpoint advancement. Metadata-only tails can advance the
checkpoint directly because later details append with newer sequences.
AI completions carry request IDs, reuse a single batch mailbox, and reject late
completions belonging to timed-out operations.

HTTPS owns an atomic resource state through cleanup. Audio reminders defer and
coalesce until it releases ownership, preventing concurrent I2S DMA and TLS
allocations. Muting still cancels deferred reminders. Certificate validation
remains enabled. The archive reserves 512 KiB for digests and 32 KiB for filesystem
overhead. Full journals or digest storage stop the corresponding writes without
overwriting history. Full or torn digest storage blocks new API work before
requests, avoiding repeated charges for results that cannot be saved. Larger
existing journals remain intact but may become read-only under the new budget.

Host stress tests exercise the actual archive implementation with 500 complete
notifications and 1,000 snapshots. Eight-record batches require 63 collections
and exactly 1,000 record reads. Coverage includes reboot indexing, consecutive
bidirectional browsing, duplicate/cancel/full/wraparound queue behavior,
sensitive text beyond the truncated preview, and digest capacity. Revision twelve
also tests a 500-source metadata burst followed by its 500 detail records, duplicate
source updates, fingerprint collisions and reboot reindexing.
provider requests were made. These tests do not establish board Flash throughput,
wireless coexistence, TLS heap margin, or acceptance of hundreds of actual iPhone
notifications. Revision seven was flashed, revealing a leftover WL handle after failed FAT
mounting. Revision eight fixed recovery and cleared the old archive with explicit
user authorization. Revision nine completed actual Flash stress acceptance: 500 synthetic notifications, 1,000 snapshots, no capture drops or archive errors.


## Actual filesystem memory and recovery correction

Revision eight distinguishes a mounted WL layer from a registered FAT volume.
ESP-IDF 5.5.3 can leave a WL handle after FAT mounting fails. That path uses the
WL unmount API rather than the FAT unmount API requiring a registered context.
Cleanup failures still prevent erasure. A successfully mounted FAT volume with
a damaged journal still uses FAT unmount. Host fault injection covers both paths.
With explicit user authorization, native recovery erased only
`0x400000–0x7fffff`, formatted it, and mounted successfully.

`notification-scalable-9` disables individual FAT file caches. The single archive
worker shares a sector cache. Measured `FIL` size shrank from 4,136 to 40 bytes,
saving 8 KiB across the two file slots; disk format and partitions are unchanged.
The UI pool was reduced from 24 to 20 KiB based on measured usage. Revision eleven
renders and scrolls a full 1,023-byte Chinese digest on the board; LVGL allocation
failure and sound settings report no errors. The remaining pages have not all been
visually accepted with a physical phone connected. Diagnostic command five forcibly replaces
any mailbox key with an invalid test key, sending eight maximum-length compact
previews to exercise HTTPS memory. Authentication rejection is expected; it never
uses the mailbox's real key to invoke a model. Production disables this entry.


## Revision eleven: mounted-archive HTTPS memory

The default digest file is now `/archive/digest.dat`, compatible with the
configured FAT 8.3 names. The former longer filename prevented digest preflight
on the board although ordinary host filesystems accepted it. Host tests now
exercise the production default path with an 8.3-aware filesystem stub.
The blank-partition scan uses a 512-byte buffer. Archive and AI task stacks are
4 KiB and 5 KiB, respectively, based on measured board stack usage. The embedded
web settings server pauses during HTTPS, releasing its stack and sockets, then
restarts after cleanup; the hotspot remains running. JSON allocation failures
abort cleanly before sending incomplete notification data. Certificate and
hostname validation remain enabled.

## Public test release

The public test build starts with a light appearance and offers a dark appearance,
notification sound controls, Wi-Fi, and AI settings in the password-protected
local hotspot portal. The first setup hotspot is open so a phone can connect;
keep it nearby and do not expose it to untrusted networks. The portal uses HTTP,
and saved settings are stored in device NVS.

This release does not include synthetic notification fixtures or device stress-test
commands. The portal's archive-clear action removes notification history and AI
summary progress while retaining Wi-Fi, AI, theme, and sound settings.

Notification tones use two 160-frame I2S DMA descriptors per direction instead
of the BSP default six 240-frame descriptors, reducing initial stereo DMA
buffers from 11,520 to 2,560 bytes. The shared BSP retains its original defaults.
Remote navigation waits for pending archive reads before returning screen text.

The application selector shows one focused app card with a friendly name and
notification count. The home hint exposes the long-UP summary shortcut and
long-OK settings shortcut; neighbouring applications do not repeat in the card.
