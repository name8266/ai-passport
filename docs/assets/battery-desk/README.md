<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Battery Desk

Battery Desk turns the FoloToy AI Passport into a small local battery asset manager.
Development starts from `main` on `feature/battery-assets`. The firmware uses the
existing BSP in `components/bsp`; all asset, storage, clock, networking and UI logic
lives in `main`. The application does not compile or navigate to baseline demo UI.

## What is included

- A redesigned portrait device UI: overview, numbered asset cards, action selection
  and confirmation, and the phone workspace connection screen.
- A responsive browser workspace with inventory counts, status distribution,
  lowest-charge chart, searchable/filterable asset table, detail/edit dialogs,
  operation history, CSV export and full JSON export.
- Sixteen assets and the latest 48 audit events in a versioned, CRC-checked NVS
  snapshot. IDs are stable and never reused. Removing an asset requires retirement
  first, explicit confirmation, and does not remove its surviving audit entries.
- Atomic publication after successful `nvs_set_blob` and `nvs_commit`. Conflicting
  revisions are rejected. Storage initialization/corruption never triggers an
  automatic partition erase; the app shows storage unavailable and refuses writes.
- An on-demand, single-client WPA2 SoftAP with a generated 12-character key shown
  only on the device. The key changes whenever the workspace starts. The browser
  is embedded and compressed; no CDN, Internet connection or paid service is needed.
- Phone clock synchronization on page open, refresh, foreground return, and about
  once a minute while visible. The phone's Unix timestamp and timezone offset are
  transmitted to the device. The monotonic clock advances time between syncs.

## Start using it

1. Hold `OK` on the device overview to open the phone workspace, then click `OK`
   to start Wi-Fi. Alternatively, click `DOWN` from the overview.
2. Join the `BatteryDesk-XXXX` network using the key displayed on the device.
   Keep the connection even if the phone reports that this Wi-Fi has no Internet.
3. Open **http://192.168.4.1/** in your phone browser. The clock syncs automatically.
4. Add battery name, chemistry, nominal capacity, charge, health, cycles, location
   and notes. Use the detail dialog for status changes.
5. Use the device to browse `BAT-001` IDs and confirm quick operations. Use `OK`
   on the phone workspace screen to stop Wi-Fi when you have finished.

The top-right device percentage is the Passport's own optional CW2017 reading.
Asset charge and health are user-entered inventory data. This application does
not measure, switch or charge external batteries. It does not add wiring.

## Device controls

| Screen | UP / DOWN click | OK click | OK hold |
| --- | --- | --- | --- |
| Overview | Phone workspace | Browse assets | Phone workspace |
| Asset cards | Previous / next asset, wrapping | Action picker; empty inventory opens phone workspace | Overview |
| Action picker | Choose operation; cancel pending confirmation | First selects, second confirms and persists | Asset card |
| Phone workspace | Overview | Start / stop Wi-Fi | Overview |

Double-click and non-OK long presses have no assigned action. A press after the
backlight turns off only wakes the screen and cannot execute an asset operation.
The backlight dims to 15% after 45 seconds without device input and turns off
at 120 seconds. Returning to the overview does not stop an active workspace;
stop it explicitly from its screen. The running local server requires an awake
CPU; deep sleep is not implemented or claimed.

Device UI uses English and supported Montserrat 14/20/28 fonts. Arbitrary UTF-8
asset names remain in the browser. Stable ASCII `BAT-xxx` IDs are displayed on
the device so Chinese names cannot become missing-glyph boxes. Full names and
locations are preserved and can be exported.

## Data and state rules

| Current state | Allowed operations |
| --- | --- |
| Ready | Check out, start charge, inspect, retire |
| In use | Return, retire |
| Charging | Finish charge, retire |
| Service | Finish service, start charge, retire |
| Retired | Edit metadata, delete |

Metadata can be edited in every state. Status changes use the audited operation
endpoint; editing cannot bypass the state machine. Finishing charge sets the
registered charge to 100% and increments cycles, so confirm the physical charge
has finished. Retiring an asset is terminal. A full cycle counter rejects charge
completion; correct metadata before retrying. The attention count includes active
assets with charge at most 20%, health below 80%, or service status.

Names, locations and notes support validated UTF-8, with respective limits of
63, 47 and 95 **bytes**. Controls and invalid UTF-8 are rejected. Capacity is
1–60000 mAh, percentages 0–100, cycles 0–65535. New browser assets start ready.
Firmware uses the original NVS/PHY/factory partition layout and a dedicated
`battery_v1` namespace. Schema 1 stores a fixed-layout snapshot, validates its
size/version/CRC on boot, and fails closed on unknown layouts. No migration from
other applications is attempted and unrelated namespaces are not erased.

The audit buffer keeps the latest 48 entries, evicting the oldest on the next
operation. Export regularly for longer history. CSV exports all asset rows,
regardless of current filters. JSON exports all retained assets/events, but this
version does not include a restore/import endpoint. Exported files may include
your asset names, locations and notes; store them as you would other inventory data.

Cold boot intentionally starts with **unknown current time**. A saved timestamp
cannot establish how long power was absent. Offline actions still persist with
epoch 0 and are labelled unsynchronized; later sync never invents dates for
those old events. Valid sync timestamps span 2024-01-01 through 2100-01-01 and
timezone offsets -12 through +14 hours. Clock quality follows the phone's system
clock, not an independent time authority. Page requests are serialized and
bounded; HTTP mutations require a matching local Host/Origin, JSON content type
and custom header. The SoftAP password controls local access; this is a private
single-user workspace, without Internet hosting or user accounts.

If another client/device operation changes data while an edit is open, saving
rejects the stale revision and retains the draft. Close/reopen the editor to
review the latest data before saving again. A network timeout can leave the
save result unknown: refresh and inspect the history before retrying, especially
charge completion. There are no automatic mutation retries.

## Build and tests

Use the five installed Passport skills and ESP-IDF **5.5.3**:

```bash
source <esp-idf-v5.5.3>/export.sh
./tools/validate.sh --static
./tools/validate.sh --firmware
./tools/validate.sh
```

The shared static gate includes model/protocol tests and storage fault injection,
plus the existing baseline tests. It needs no attached board. Model tests cover
transitions, revision conflicts, finite bounds, UTF-8, CRC/schema rejection,
rolling history and clock behavior. Store tests compile the actual storage module
with NVS/mutex fault stubs and verify write/commit failures do not publish a change.
The final gate retains a merged image and matching ELF/MAP debug bundle under
`build/firmware/<full-bin-sha256>/`; verify it with
`python3 tools/archive_firmware.py verify <archive-directory>`.

For a browser preview with synthetic data and the actual C state model:

```bash
python3 tests/battery_web_preview.py --seed
# Open http://127.0.0.1:8765/
```

Without `--seed`, the preview begins empty. It holds synthetic data in memory and
is not an emulator of ESP-IDF HTTP, Wi-Fi, NVS or the physical LCD. For repeatable
browser tests, make Playwright available to Node and install Chromium, then run:

```bash
NODE_PATH=<directory-containing-playwright> node tests/test_battery_web.cjs
```

The script uses `/usr/bin/chromium` unless `CHROMIUM_PATH` is supplied. It starts
its own loopback fixture on port 8766 and tests clock sync, CRUD/status operations,
conflicts with draft preservation, UTF-8 limits, escaping, filtering, downloads,
disconnect/recovery and 1440/390/320 px layouts. It saves synthetic screenshots and
exports under `build/previews/`. This optional browser test is separate from the
shared gate because it requires a browser installation.

Flash the verified `build/FoloToy-AI-Passport-full.bin` at **0x0** only after
explicit permission. The merged image may reset stored data; export first.
A compatible segmented flash is needed to preserve existing NVS, as described in
[firmware layout](../../development/engineering/firmware-layout.md).
Never write the application-only binary at 0x0. USB flashing cannot reach your
computer from a cloud workspace without an explicitly available USB bridge.

## Physical-device acceptance

Every item below remains NOT RUN until observed on a real Passport. A compiler,
host test, browser fixture or successful flash does not count as device acceptance.

| Item | Procedure | Expected result |
| --- | --- | --- |
| Boot and original UI removal | Boot the exact verified firmware; navigate every screen | New overview/card/action/workspace UI only; no demo test menu |
| Portrait display | Visit every screen, empty state, error message and three action rows | Correct colors, readable text, no clipping at rounded corners; top-right own battery reading or `--` |
| Three-key behavior | Exercise each click/hold, cancel confirmation, rapidly switch assets | Correct selection and back navigation; no double action; callbacks remain responsive |
| Mobile workflow | Connect Android and iOS, open 192.168.4.1, add/edit/filter assets | DHCP, compressed page, tables and charts work; write success updates device within its refresh interval |
| Clock | Set a known phone time/timezone; open/foreground/refresh page; compare device | Display matches phone within transmission delay; event epoch follows phone; reboot is unknown until re-sync |
| Offline persistence | Stop Wi-Fi; perform an operation; cut power after save confirmation and reboot | Asset state and history persist; pre-sync events remain explicitly undated |
| Interrupted NVS save | On expendable test data, cut power during saves over repeated trials | Recover a valid previous or completed snapshot; never a silently accepted corrupt record |
| Capacity and history | Fill 16 assets; attempt the 17th; execute more than 48 operations; export | 17th rejected, newest 48 ordered events retained, full exports readable |
| Web/device conflict | Keep a web editor open; change the asset on the device; save stale editor | Revision conflict; draft retained; latest data reloads without an overwrite |
| Invalid requests | Submit oversized/malformed/deep JSON, invalid dates and foreign origins | 400/403/413 or controlled timeout; no mutation, crash or automatic retry |
| Reconnect/lifecycle | Start/stop workspace 20 times; disconnect/rejoin phones and interrupt requests | No stuck sockets/tasks/netif; key changes each start; offline error and recovery work |
| Memory/stack | Record heap/largest block at AP start and full-history fetch; check task high-water marks | Stable allocation over repeated cycles; adequate contiguous memory and worker stacks, no resets |
| Battery degradation | Test a board without readable CW2017 | `--` shown; inventory and Web service still operate |
| Idle screen | Wait 45 s then 120 s; press a button after blanking | Dim then off; first press wakes only; enabled web stays available |
| Data preservation on upgrade | Export; perform compatible segmented reflash or deliberate full refresh | Segmented compatible update preserves data; merged refresh impact is understood |

## References consulted

The fork did not contain demo branches, so only the relevant upstream patterns
were fetched for inspection, without merging their UI, BSP or configuration:

- `demo/cat-themed-pomodoro-timer` at `4086b9e8e9e0eca2b547a83560cebb4e3b40059a`:
  separated model/storage/UI and NVS worker patterns. Its erase-on-init recovery
  is deliberately not copied.
- `demo/blufi-provisioning` at `9c039cc5127f22072afa83bedb7fa3d8efe635ad`:
  one-time network/event initialization. Bluetooth provisioning is not needed for
  this local AP-only management app.
- [SoftAP resource budget](../../reference/phoenixzhc/softap-provisioning-and-resource-budget.md):
  AP-only management, bounded HTTP, lazy optional peripherals, no-PSRAM budgets.
- [Hardware guide](../../hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md) and
  [AI guide](../../development/ai-guide.md): BSP ownership, locking, input callbacks,
  mandatory UI redesign and separate physical acceptance.
