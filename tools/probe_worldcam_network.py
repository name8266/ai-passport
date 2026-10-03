#!/usr/bin/env python3
"""Decode public camera images from this computer; never modify the catalogue.

Run locally: python tools/probe_worldcam_network.py --output worldcam-network-report.json
Requires Pillow (see gateway/requirements.txt). HTTP environment proxies are
disabled by default; VPNs, system routing and the physical exit are not verified.
"""
import argparse
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
from io import BytesIO
import json
import os
from pathlib import Path
import re
import socket
import ssl
import time
import urllib.error
import urllib.parse
import urllib.request

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
MAX_BYTES = 8 * 1024 * 1024
MAX_PIXELS = 20_000_000
Image.MAX_IMAGE_PIXELS = MAX_PIXELS
PROXY_KEYS = ('http_proxy', 'https_proxy', 'all_proxy', 'HTTP_PROXY', 'HTTPS_PROXY', 'ALL_PROXY')


def utc_now():
    return datetime.now(timezone.utc).isoformat(timespec='seconds')


def public_url(value):
    parts = urllib.parse.urlsplit(value)
    if parts.scheme not in ('http', 'https') or not parts.hostname or parts.username or parts.password:
        raise ValueError('Invalid public source URL')
    return value


def resolve_usap(metadata):
    match = re.match(r'^([A-Za-z0-9_-]+\.jpg)(?:\?[^,]*)?,', metadata.strip())
    if not match:
        raise ValueError('Invalid webcam metadata')
    return 'https://www.usap.gov/videoClipsAndMaps/SouthPoleWebcam/' + match[1]


def decode_image(raw):
    if len(raw) > MAX_BYTES:
        raise OverflowError('Source image exceeds byte limit')
    with Image.open(BytesIO(raw)) as image:
        if image.width * image.height > MAX_PIXELS:
            raise OverflowError('Source image exceeds pixel limit')
        image.load()
        image.convert('RGB').load()
        return {'width': image.width, 'height': image.height, 'format': image.format}


def network_failure(error):
    reason = error.reason if isinstance(error, urllib.error.URLError) else error
    if isinstance(reason, (TimeoutError, socket.timeout)):
        return 'timeout'
    if isinstance(reason, ssl.SSLError):
        return 'tls_error'
    if isinstance(reason, socket.gaierror):
        return 'dns_error'
    return 'network_error'


def probe_camera(camera, timeout=12, use_env_proxy=False, opener=None):
    """One attempt, including USAP metadata resolution and actual pixel decoding."""
    began = time.monotonic()
    source = camera.get('snapshot', '')
    result = {'id': camera['id'], 'provider': camera.get('source', 'Unknown'),
              'source_host': None,
              'checked_at': utc_now(), 'http_status': None}
    phase = 'network'
    try:
        public_url(source)
        result['source_host'] = urllib.parse.urlsplit(source).hostname
        if opener is None:
            handler = urllib.request.ProxyHandler() if use_env_proxy else urllib.request.ProxyHandler({})
            opener = urllib.request.build_opener(handler)
        resolver = camera.get('resolver')
        if resolver and resolver != 'usap':
            raise ValueError('Unsupported resolver')
        if resolver == 'usap':
            with opener.open(source, timeout=timeout) as response:
                result['metadata_http_status'] = response.status
                metadata = response.read(4097)
            if len(metadata) > 4096:
                raise ValueError('Webcam metadata exceeds limit')
            source = resolve_usap(metadata.decode('utf-8'))
        request = urllib.request.Request(source, headers={
            'User-Agent': 'PassportWorldCam/2.0', 'Accept': 'image/*'})
        with opener.open(request, timeout=timeout) as response:
            result['http_status'] = response.status
            result['image_host'] = urllib.parse.urlsplit(response.geturl()).hostname
            raw = response.read(MAX_BYTES + 1)
        result['bytes'] = len(raw)
        phase = 'decode'
        result.update(decode_image(raw))
        result['status'] = 'image_decoded'
    except urllib.error.HTTPError as error:
        result.update(status='http_error', http_status=error.code, error_type=type(error).__name__)
    except (OverflowError, Image.DecompressionBombError) as error:
        result.update(status='image_too_large', error_type=type(error).__name__)
    except (OSError, urllib.error.URLError) as error:
        result.update(status='image_decode_error' if phase == 'decode' else network_failure(error),
                      error_type=type(error).__name__)
    except (ValueError, UnicodeError, SyntaxError) as error:
        result.update(status='image_decode_error' if phase == 'decode' else 'invalid_source',
                      error_type=type(error).__name__)
    finally:
        result['elapsed_ms'] = round((time.monotonic() - began) * 1000)
    return result


def build_report(cameras, results, coverage, started_at, use_env_proxy=False, vantage_label='unverified'):
    tested = {result['id'] for result in results}
    passed = {result['id'] for result in results if result['status'] == 'image_decoded'}
    by_provider = defaultdict(list)
    for result in results:
        by_provider[result['provider']].append(result)
    providers = {provider: {'tested': len(rows),
                 'passed': sum(row['status'] == 'image_decoded' for row in rows),
                 'outcomes': dict(Counter(row['status'] for row in rows))}
                 for provider, rows in sorted(by_provider.items())}
    return {'schema_version': 1, 'started_at': started_at, 'finished_at': utc_now(),
            'vantage': {'label': vantage_label, 'verified': False,
                        'note': 'Operator supplied label only; country, VPN and physical egress are not verified.'},
            'network_mode': 'environment_proxy' if use_env_proxy else 'application_proxies_disabled',
            'proxy_environment_configured': any(os.environ.get(key) for key in PROXY_KEYS),
            'method': 'One HTTP attempt per image, TLS verification enabled, full pixel decoding; capture freshness not established.',
            'catalogue_cameras': len(cameras), 'tested': len(results), 'passed': len(passed),
            'failed': len(results) - len(passed),
            'sample': 'all' if len(results) == len(cameras) else 'catalogue prefix; not a representative sample',
            'capitals': {'registered': len(coverage),
                         'without_catalogued_source': sum(not c['camera_ids'] for c in coverage),
                         'with_tested_source': sum(bool(tested.intersection(c['camera_ids'])) for c in coverage),
                         'with_decoded_image': sum(bool(passed.intersection(c['camera_ids'])) for c in coverage)},
            'providers': providers, 'results': results}


def main(argv=None):
    parser = argparse.ArgumentParser(description='在本机检查公开摄像头图片，默认禁用 HTTP 环境代理。')
    parser.add_argument('--limit', type=int, default=0, help='仅检查目录前 N 个；0 检查全部，抽样不代表整体')
    parser.add_argument('--workers', type=int, choices=range(1, 5), default=3, help='并发数 1–4')
    parser.add_argument('--timeout', type=int, choices=range(3, 31), default=12, help='单次请求超时秒数 3–30')
    parser.add_argument('--use-env-proxy', action='store_true', help='改为使用 urllib 的环境代理配置')
    parser.add_argument('--vantage-label', default='unverified', help='自填测试网络名称，报告仍标为未经独立验证')
    parser.add_argument('--output', type=Path, default=Path('worldcam-network-report.json'))
    args = parser.parse_args(argv)
    if args.limit < 0:
        parser.error('--limit must be nonnegative')
    if not 1 <= len(args.vantage_label) <= 80 or any(ord(c) < 32 for c in args.vantage_label):
        parser.error('--vantage-label must be 1–80 characters without control characters')
    cameras = json.loads((ROOT / 'gateway/cameras.json').read_text(encoding='utf-8'))
    coverage = json.loads((ROOT / 'gateway/capital-coverage.json').read_text(encoding='utf-8'))
    selected = cameras[:args.limit] if args.limit else cameras
    started_at = utc_now()
    print('测试出口：' + args.vantage_label + '（未经独立验证）', flush=True)
    print('应用代理：' + ('使用环境配置' if args.use_env_proxy else '禁用；无法检测 VPN 或系统路由'), flush=True)
    results = []
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        pending = {pool.submit(probe_camera, c, args.timeout, args.use_env_proxy): c['id'] for c in selected}
        for future in as_completed(pending):
            results.append(future.result())
            if len(results) % 50 == 0 or len(results) == len(selected):
                passed = sum(row['status'] == 'image_decoded' for row in results)
                print(f'已检查 {len(results)}/{len(selected)}，成功解码 {passed}', flush=True)
    order = {c['id']: i for i, c in enumerate(cameras)}
    results.sort(key=lambda r: order[r['id']])
    report = build_report(cameras, results, coverage, started_at, args.use_env_proxy, args.vantage_label)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f"可取得并解码图片：{report['passed']}/{report['tested']}；首都：{report['capitals']['with_decoded_image']}/{len(coverage)}")
    for provider, count in report['providers'].items():
        print(f"  {provider}: {count['passed']}/{count['tested']}")
    print('报告：' + str(args.output))
    print('结果仅代表此次本机访问；不证明图片拍摄时间或整个中国大陆的可用性。')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
