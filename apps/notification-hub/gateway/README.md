# Passport AI Notification Gateway (DeepSeek by default)

This is a **source-only/uncompiled prototype** on \`feature/notification-archive\`.
The user explicitly requested NO firmware compilation and NO deployment yet.
Its job is to keep third-party model API keys off the ESP32-C3 and provide
a web admin page for model/provider/privacy/schedule.

## Architecture

\`\`\`
iPhone ANCS --BLE--> Passport Notification Archive (4 MiB Flash)
                           |
                           | every configured 15–1440 minutes (HTTPS + token)
                           | only unsummarized, user-enabled notification previews
                           v
                 Self-hosted Python gateway (Mac/NAS/VPS)
                   |  /admin protected configuration
                   |  never persist raw notifications
                   +--> DeepSeek HTTPS API (configurable OpenAI-compatible)
                           |
                           v
 Passport <- summary JSON <- gateway (AI model key NEVER sent to device)
       |  append AI digest on Flash; only then acknowledge cursor in NVS
       +--> Long UP: paged AI summary on 240×320 screen
\`\`\`

The notification archive is independent from the summary progress. On a
gateway outage, the local full archive remains. The AI batches are retried
until acknowledged. At most four batches of eight notifications are processed
per wake cycle by this prototype, limiting runaway costs. Subsequent
sessions continue the backlog. iPhone need not be awake when stored entries
are summarized, but Passport must be powered and online over Wi-Fi.

## Security and default privacy policy

- **AI uploading is disabled by default**, until the user opts in via /admin.
- App filtering by App Identifier; exclude notifications containing verification
  codes, passwords and banking keywords by default; user can change this.
- HTTPS with CA verification between Passport and gateway. TLS is required
  for browser admin access too; don't expose Basic Auth over plain HTTP.
- DeepSeek API key can be changed on admin page. At rest it is encrypted
  using Fernet key provided via \`HUB_ENCRYPTION_KEY\` environment variable.
  The key must never be committed to git.
- Device authentication via \`HUB_DEVICE_TOKEN\`; Passport stores that token
  and Wi-Fi credentials in NVS. **Prototype NVS may be readable by physical
  Flash access**. Production requires Flash encryption and a stronger
  physical provisioning procedure.
- Gateway keeps raw notification batches only in process memory for each
  request; it does not persist raw or digest text. Passport does persist
  both original ANCS captures and AI digest previews in its archive area.
- The AI vendor processes any text sent to it. Such usage is not the same as
  on-device Apple Intelligence. Carefully review third-party retention,
  pricing, privacy rules and cross-border transfer requirements.
- Notifications contain **untrusted text**. Prompt specifies they are
  data, never executable instructions. This does not provide a complete
  guarantee against prompt-injection attacks.

## Development-only gateway setup

Python 3.11+:

\`\`\`bash
cd apps/notification-hub/gateway
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python3 -c "from cryptography.fernet import Fernet; print(Fernet.generate_key().decode())"
\`\`\`

Set environment variables from \`.env.example\` **using your own** strong
password/token/key. Do not commit \`.env\`. Example:

\`\`\`bash
set -a
. ./.env
set +a
uvicorn app:app --host 127.0.0.1 --port 8765
\`\`\`

Open \`/admin\` locally with Basic Auth username \`admin\`. For a device on
your network, publish it through a reverse proxy with a **trusted HTTPS
certificate** and a DNS hostname that the device can reach. For example
\`https://passport-ai.example.com\`. The on-device HTTP client does not
disable certificate checks. Local IP / self-signed TLS requires deliberately
provisioning a trusted CA, not ignoring errors.

The admin page controls provider label, HTTPS chat-completions endpoint,
model, encrypted API key, interval, per-batch cap, excluded apps and
sensitive keyword filtering. The default endpoint is
\`https://api.deepseek.com/chat/completions\`, model
\`deepseek-v4-flash\` (per October 2026 DeepSeek V4 API docs). Other
OpenAI-compatible models are supported when the provider implements this
schema. Not every provider supports every generation parameter.

The process must remain running on a Mac, NAS, or server. The server itself
does not independently read iPhone notifications; it only receives batches
when the Passport sends them at the configured interval.

## One-time Passport Wi-Fi configuration (development mode)

From a USB-connected ESP-IDF serial monitor, enter:

\`\`\`text
hub help
hub wifi MyWiFi|MyWiFiPassword
hub server https://passport-ai.example.com
hub token A_VERY_LONG_RANDOM_SHARED_DEVICE_TOKEN
hub restart
\`\`\`

The token must match \`HUB_DEVICE_TOKEN\` on the gateway. The password may
be echoed and visible in the serial monitor session; use a trusted computer
and do not share its transcript. These settings persist in NVS even when
Passport loses power. Subsequent model/schedule changes happen in /admin;
the gateway checks configuration each minute, without firmware reflashing.

Please do not push a public certificate/private key/SSID/token/actual
notification screenshots to GitHub.

## Pass/fail boundaries

No CI, local compilation, actual device runs, TLS pairing or BLE+Wi-Fi
coexistence tests have been performed for this branch. On the ESP32-C3 with
no PSRAM, concurrent Bluedroid, LVGL, Wi-Fi and TLS may exceed available
heap in practice; measure minimum and largest contiguous free heap on real
hardware before enabling frequent summaries.

The current UI only displays the **latest saved AI digest**, paged
by 60 Unicode characters. Digest history exists in Flash but browsing
older AI digests is not yet implemented. The archival quota is finite.
There is no automatic reset of the archive or summary checkpoint.

## Backend contract

- \`GET /api/device/config\` requires \`Authorization: Bearer <token>\`;
  returns enabled, interval_minutes, max_records, redact_sensitive.
- \`POST /api/device/summarize\` same auth, JSON
  \`{device_id, after_sequence, through_sequence, notifications:[{seq,app,title,body}]}\`;
  returns \`{processed_through, summary, included}\`.
- Summary confirmation is acknowledged locally only after the digest has
  been durably written to the Passport. Gateway does not store raw payloads.
- No Android notification listener, private iOS API, or reading of iPhone
  content outside ANCS.

Official DeepSeek docs: https://api-docs.deepseek.com/
Apple ANCS: https://developer.apple.com/library/archive/documentation/CoreBluetooth/Reference/AppleNotificationCenterServiceSpecification/Specification/Specification.html
