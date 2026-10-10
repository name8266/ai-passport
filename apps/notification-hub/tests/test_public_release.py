#!/usr/bin/env python3
"""Public firmware delivery checks; no network, no third-party libraries."""
from pathlib import Path
import hashlib
import re
import sys

ROOT=Path(__file__).resolve().parents[3]
HUB=ROOT/"apps/notification-hub"
MAIN=HUB/"firmware/main"
public_sources=[*MAIN.glob("*.c"),*MAIN.glob("*.h")]
assert public_sources, "Production source tree missing"

# Production source and flash image must not contain fictional notifications,
# demonstration API credentials or previous beta product branding.
forbidden=["com.example.app","示例：{","notification-public-beta-19",
           "准备演示","keep-key","keep-password","演示通知","测试账号"]
for f in [*public_sources,HUB/"firmware/CMakeLists.txt"]:
    data=f.read_text("utf-8")
    for needle in forbidden:
        assert needle not in data, f"{f}: forbidden public-release residue {needle!r}"

controller=(MAIN/"app_main.c").read_text("utf-8")
envelope=(MAIN/"hub_control_event.h").read_text("utf-8")
assert 'archive_control_event_t command=hub_control_make(ARCHIVE_CLEAR);' in controller
assert 'archive_control=xQueueCreate(2,sizeof(archive_control_event_t))' in controller
assert 'uint8_t command=ARCHIVE_CLEAR' not in controller
assert 'xQueueSend(archive_control,&command' in controller
assert '#include "hub_control_event.h"' in controller
assert "fingerprint" in envelope and "revision" in envelope

settings=(MAIN/"hub_ai_settings.c").read_text("utf-8")
assert "s->enabled=false;" in settings
assert "s->redact_sensitive=true;" in settings
assert 'read_str(h,"api_key"' in settings
assert "NVS_READONLY" in settings
assert 's->api_key[0]' in settings
web=(MAIN/"hub_ai_web.c").read_text("utf-8")
assert '"/summary/complete"' in web
assert '"/summary/show"' in web
assert "com.example.app" not in web
assert "已汇总记录" in web
assert "未汇总" in web
assert "逐项" in web

# Optional artifact scan checks the ACTUAL customer binaries and verifies the
# merged image ends before the 0x400000 FAT data partition.
if len(sys.argv)>1:
    folder=Path(sys.argv[1])
    full=list(folder.glob("*全新安装.bin"))
    app=list(folder.glob("*应用升级.bin"))
    assert len(full)==1 and len(app)==1, "Need one fresh and one application BIN"
    assert 100_000<full[0].stat().st_size<0x400000, "Full image would overwrite archive"
    assert 100_000<app[0].stat().st_size<0x3F0000, "Application exceeds factory partition"
    assert app[0].read_bytes()[:2]!=b"PK", "Application BIN accidentally ZIP"
    manifest=folder/"校验值-SHA256.txt"
    assert manifest.exists(), "Checksums absent"
    checks=manifest.read_text("utf-8")
    for f in (full[0],app[0]):
        data=f.read_bytes()
        for needle in forbidden:
            assert needle.encode("utf-8") not in data, f"Embedded sample {needle!r}"
        digest=hashlib.sha256(data).hexdigest()
        assert f.name in checks and digest in checks, f"Bad checksum entry for {f.name}"
    install=folder/"客户安装说明.md"
    assert install.exists() and "erase_flash" in install.read_text("utf-8")
    print("Customer binaries, partition boundary, clean image, UTF-8 names and SHA256: PASS")
print("Public source defaults, queue shape and sample-data hygiene: PASS")
