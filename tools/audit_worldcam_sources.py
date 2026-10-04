#!/usr/bin/env python3
"""Verify public WorldCam snapshots against the actual Passport decoder contract.

A camera is firmware-eligible only when every probe attempt:
- returns HTTP 200 within the configured latency budget,
- is <= 4 MiB,
- is a baseline (SOF0) JPEG, not PNG/WebP/progressive JPEG,
- fully decodes with Pillow,
- and is not obviously stale when Last-Modified is available.

The script updates cameras.json, capital coverage and source-audit.json. It never
adds new URLs and never treats browser-only/video pages as snapshot sources.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
from email.utils import parsedate_to_datetime
from io import BytesIO
import hashlib
import json
from pathlib import Path
import re
import socket
import ssl
import time
import urllib.error
import urllib.parse
import urllib.request

from PIL import Image, UnidentifiedImageError

ROOT = Path(__file__).resolve().parents[1]
CAMERAS = ROOT / "gateway/cameras.json"
COVERAGE = ROOT / "gateway/capital-coverage.json"
AUDIT = ROOT / "gateway/source-audit.json"
MAX_BYTES = 4 * 1024 * 1024
MAX_PIXELS = 20_000_000
Image.MAX_IMAGE_PIXELS = MAX_PIXELS
USER_AGENT = "PassportWorldCam/5.0"
USAP_PREFIX = "https://www.usap.gov/videoClipsAndMaps/SouthPoleWebcam/"


def now_epoch():
    return int(time.time())


def resolve_usap(metadata):
    match = re.match(r"^([A-Za-z0-9_-]+\.jpg)(?:\?[^,]*)?,", metadata.strip())
    if not match:
        raise ValueError("invalid_usap_metadata")
    return USAP_PREFIX + match.group(1)


def jpeg_profile(raw):
    if len(raw) < 4 or raw[:2] != b"\xff\xd8":
        return "not_jpeg"
    i = 2
    while i + 3 < len(raw):
        if raw[i] != 0xFF:
            i += 1
            continue
        while i < len(raw) and raw[i] == 0xFF:
            i += 1
        if i >= len(raw):
            break
        marker = raw[i]
        i += 1
        if marker in (0xD8, 0xD9) or 0xD0 <= marker <= 0xD7 or marker == 0x01:
            continue
        if marker == 0xDA:
            break
        if i + 2 > len(raw):
            break
        seg = (raw[i] << 8) | raw[i + 1]
        if seg < 2 or i + seg > len(raw):
            break
        if marker == 0xC0:
            return "baseline"
        if marker == 0xC2:
            return "progressive"
        i += seg
    return "unknown_jpeg"


def last_modified_age(value, checked_at):
    if not value:
        return None
    try:
        dt = parsedate_to_datetime(value)
        if dt.tzinfo is None:
            dt = dt.replace(tzinfo=timezone.utc)
        return max(0, int(checked_at - dt.timestamp()))
    except Exception:
        return None


def public_url(value):
    parts = urllib.parse.urlsplit(value)
    if parts.scheme not in ("http", "https") or not parts.hostname or parts.username or parts.password:
        raise ValueError("invalid_public_url")
    return value


def fetch_once(camera, timeout, max_ms, opener):
    started = time.monotonic()
    checked_at = now_epoch()
    result = {
        "http_status": None,
        "elapsed_ms": None,
        "bytes": 0,
        "format": None,
        "jpeg_profile": None,
        "width": None,
        "height": None,
        "last_modified": None,
        "last_modified_age_s": None,
        "digest16": None,
        "ok": False,
        "reason": None,
    }
    try:
        source = public_url(camera.get("snapshot", ""))
        if camera.get("resolver") == "usap":
            with opener.open(urllib.request.Request(source, headers={"User-Agent": USER_AGENT}), timeout=timeout) as response:
                if response.status != 200:
                    result["http_status"] = response.status
                    raise ValueError("resolver_http")
                metadata = response.read(4097)
            if len(metadata) > 4096:
                raise ValueError("resolver_too_large")
            source = resolve_usap(metadata.decode("utf-8"))

        request = urllib.request.Request(source, headers={
            "User-Agent": USER_AGENT,
            "Accept": "image/jpeg,image/jpg;q=0.9,*/*;q=0.1",
            "Cache-Control": "no-cache",
            "Pragma": "no-cache",
        })
        with opener.open(request, timeout=timeout) as response:
            result["http_status"] = response.status
            result["last_modified"] = response.headers.get("Last-Modified")
            raw = response.read(MAX_BYTES + 1)
        result["bytes"] = len(raw)
        result["elapsed_ms"] = round((time.monotonic() - started) * 1000)
        if result["http_status"] != 200:
            result["reason"] = "http_status"
            return result
        if len(raw) > MAX_BYTES:
            result["reason"] = "too_large"
            return result
        if result["elapsed_ms"] > max_ms:
            result["reason"] = "too_slow"
            return result

        profile = jpeg_profile(raw)
        result["jpeg_profile"] = profile
        if profile != "baseline":
            result["reason"] = profile
            return result

        with Image.open(BytesIO(raw)) as image:
            result["format"] = image.format
            result["width"], result["height"] = image.size
            if image.width * image.height > MAX_PIXELS:
                result["reason"] = "too_many_pixels"
                return result
            if image.format != "JPEG":
                result["reason"] = "not_jpeg"
                return result
            image.load()
            image.convert("RGB").load()

        age = last_modified_age(result["last_modified"], checked_at)
        result["last_modified_age_s"] = age
        if age is not None and age > 24 * 3600:
            result["reason"] = "stale_last_modified"
            return result

        result["digest16"] = hashlib.sha256(raw).hexdigest()[:16]
        result["ok"] = True
        result["reason"] = "ok"
        return result
    except urllib.error.HTTPError as exc:
        result["http_status"] = exc.code
        result["reason"] = "http_error"
    except urllib.error.URLError as exc:
        reason = exc.reason
        if isinstance(reason, (TimeoutError, socket.timeout)):
            result["reason"] = "timeout"
        elif isinstance(reason, ssl.SSLError):
            result["reason"] = "tls_error"
        elif isinstance(reason, socket.gaierror):
            result["reason"] = "dns_error"
        else:
            result["reason"] = "network_error"
    except (TimeoutError, socket.timeout):
        result["reason"] = "timeout"
    except (UnidentifiedImageError, OSError):
        result["reason"] = "decode_error"
    except Exception as exc:
        result["reason"] = type(exc).__name__
    finally:
        if result["elapsed_ms"] is None:
            result["elapsed_ms"] = round((time.monotonic() - started) * 1000)
    return result


def probe(camera, attempts, timeout, max_ms):
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    rows = [fetch_once(camera, timeout, max_ms, opener) for _ in range(attempts)]
    passed = all(row["ok"] for row in rows)
    worst_ms = max(row["elapsed_ms"] for row in rows)
    reasons = [row["reason"] for row in rows if not row["ok"]]
    return {
        "id": camera["id"],
        "provider": camera.get("source", "Unknown"),
        "snapshot_host": urllib.parse.urlsplit(camera.get("snapshot", "")).hostname,
        "checked_at": now_epoch(),
        "attempts": rows,
        "status": "snapshot_verified" if passed else "unavailable",
        "device_compatible": passed,
        "worst_elapsed_ms": worst_ms,
        "reason": "ok" if passed else (reasons[0] if reasons else "failed"),
    }


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--timeout", type=float, default=6.0)
    parser.add_argument("--attempts", type=int, default=2)
    parser.add_argument("--max-ms", type=int, default=4500)
    parser.add_argument("--min-passed", type=int, default=80)
    args = parser.parse_args(argv)
    if not 1 <= args.workers <= 24:
        parser.error("--workers must be 1..24")
    if not 1 <= args.attempts <= 3:
        parser.error("--attempts must be 1..3")

    cameras = json.loads(CAMERAS.read_text(encoding="utf-8"))
    coverage = json.loads(COVERAGE.read_text(encoding="utf-8"))
    results = []
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        pending = {pool.submit(probe, c, args.attempts, args.timeout, args.max_ms): c["id"] for c in cameras}
        for future in as_completed(pending):
            results.append(future.result())
            if len(results) % 50 == 0 or len(results) == len(cameras):
                passed = sum(r["device_compatible"] for r in results)
                print(f"checked {len(results)}/{len(cameras)} passport-compatible {passed}", flush=True)

    order = {c["id"]: i for i, c in enumerate(cameras)}
    results.sort(key=lambda row: order[row["id"]])
    by_id = {row["id"]: row for row in results}
    verified_at = now_epoch()
    for camera in cameras:
        row = by_id[camera["id"]]
        camera["status"] = row["status"]
        camera["device_compatible"] = row["device_compatible"]
        camera["verified_at"] = verified_at
        camera["latency_ms"] = row["worst_elapsed_ms"]
        camera["probe_reason"] = row["reason"]

    for item in coverage:
        item["verified_camera_ids"] = [
            camera_id for camera_id in item.get("camera_ids", [])
            if by_id.get(camera_id, {}).get("device_compatible")
        ]
        item["status"] = (
            "snapshot_verified" if item["verified_camera_ids"]
            else "listed_but_unavailable" if item.get("camera_ids")
            else "no_verified_source"
        )

    cameras_text = json.dumps(cameras, ensure_ascii=False, indent=2) + "\n"
    coverage_text = json.dumps(coverage, ensure_ascii=False, indent=2) + "\n"
    summary = {
        "schema_version": 2,
        "checked_at": verified_at,
        "contract": {
            "attempts": args.attempts,
            "timeout_s": args.timeout,
            "max_elapsed_ms": args.max_ms,
            "max_bytes": MAX_BYTES,
            "required_format": "baseline JPEG",
            "stale_last_modified_limit_s": 24 * 3600,
            "proxy_mode": "disabled",
        },
        "catalogue_cameras": len(cameras),
        "passed": sum(r["device_compatible"] for r in results),
        "failed": sum(not r["device_compatible"] for r in results),
        "results": results,
    }
    CAMERAS.write_text(cameras_text, encoding="utf-8")
    COVERAGE.write_text(coverage_text, encoding="utf-8")
    AUDIT.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Passport-compatible snapshots: {summary['passed']}/{len(cameras)}")
    if summary["passed"] < args.min_passed:
        raise SystemExit(f"too few compatible sources: {summary['passed']} < {args.min_passed}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
