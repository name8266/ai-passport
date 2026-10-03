#!/usr/bin/env python3
"""Curated public webcam and capital registry gateway for AI Passport."""
import argparse
from collections import OrderedDict
from concurrent.futures import ThreadPoolExecutor
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from io import BytesIO
import json
from pathlib import Path
import random
import re
import struct
import threading
import time
import urllib.request
import urllib.parse
from PIL import Image, ImageOps, UnidentifiedImageError

ROOT = Path(__file__).resolve().parent
CAMERAS = json.loads((ROOT / 'cameras.json').read_text())
LOCATIONS = json.loads((ROOT / 'locations.json').read_text())
COVERAGE = json.loads((ROOT / 'capital-coverage.json').read_text())
BY_ID = {c['id']: c for c in CAMERAS}
WIDTH, HEIGHT = 192, 128
FULL_WIDTH, FULL_HEIGHT = 240, 160
MAX_BYTES = 8 * 1024 * 1024
Image.MAX_IMAGE_PIXELS = 20_000_000
CACHE = OrderedDict()
CACHE_LOCK = threading.Lock()
LOCKS = {c['id']: threading.Lock() for c in CAMERAS}


def camera_usable(camera):
    return bool(camera and camera.get('snapshot') and camera.get('status') != 'unavailable')


def catalogue(query='', region='All'):
    query = query.casefold().strip()
    return [{k: v for k, v in c.items() if k not in ('snapshot', 'keywords', 'resolver')}
            for c in CAMERAS if (region == 'All' or c['region'] == region)
            and query in ' '.join(str(v) for v in c.values()).casefold()]


def locations(query='', region='All', capitals=False):
    query = query.casefold().strip()
    return [location_info(i) for i, loc in enumerate(LOCATIONS)
            if (region == 'All' or loc['region'] == region)
            and (not capitals or loc['id'].startswith('capital-'))
            and query in ' '.join(str(v) for v in loc.values()).casefold()]


def location_info(index):
    loc = LOCATIONS[index]
    camera = BY_ID.get(loc.get('camera_id'))
    return {'index': index, 'name': loc['name'], 'name_zh': loc['name_zh'],
            'country': loc['country'], 'country_zh': loc['country_zh'],
            'country_code': loc['country_code'], 'region': loc['region'],
            'lat': loc['lat'], 'lon': loc['lon'], 'capital': loc['capital'],
            'camera_id': loc.get('camera_id'), 'available': bool(camera), 'verified_available': camera_usable(camera),
            'status': camera.get('status', 'public_listing') if camera else 'no_verified_source',
            'source': camera.get('source') if camera else None,
            'page': camera.get('page') if camera else None,
            'location_precision': loc['location_precision'],
            'image_kind': camera.get('image_kind', 'snapshot') if camera else None}


def index_binary():
    if len(LOCATIONS) > 2048:
        raise ValueError('Device directory exceeds limit')
    data = bytearray(b'WCIX' + struct.pack('<HH', len(LOCATIONS), 8))
    for i, loc in enumerate(LOCATIONS):
        camera = BY_ID.get(loc.get('camera_id'))
        flags = int(camera_usable(camera)) | (int(loc['capital']) << 1) | (int(bool(camera)) << 2)
        data.extend(struct.pack('<HhhBB', i, round(loc['lat'] * 100), round(loc['lon'] * 100), flags, 0))
    return bytes(data)


def encode_frame(image, acquired, full=False):
    width, height = (FULL_WIDTH, FULL_HEIGHT) if full else (WIDTH, HEIGHT)
    image = ImageOps.pad(image.convert('RGB'), (width, height), color='#000000')
    pixels = bytearray(width * height * 2)
    for i, (r, g, b) in enumerate(image.get_flattened_data()):
        struct.pack_into('<H', pixels, i * 2, ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    return b'WCAM' + struct.pack('<HHI', width, height, acquired) + pixels


def resolve_usap(metadata):
    match = re.match(r'^([A-Za-z0-9_-]+\.jpg)(?:\?[^,]*)?,', metadata.strip())
    if not match:
        raise ValueError('Invalid webcam metadata')
    return 'https://www.usap.gov/videoClipsAndMaps/SouthPoleWebcam/' + match[1]


def fetch_frame(camera_id, full=False):
    camera = BY_ID[camera_id]
    with LOCKS[camera_id]:
        with CACHE_LOCK:
            cached = CACHE.get(camera_id)
            if cached and time.monotonic() - cached[0] < 60:
                CACHE.move_to_end(camera_id)
                return (cached[2] if full else cached[1], cached[3], cached[4])
        snapshot = camera['snapshot']
        if camera.get('resolver') == 'usap':
            with urllib.request.urlopen(snapshot, timeout=12) as response:
                snapshot = resolve_usap(response.read(4096).decode('utf-8'))
        req = urllib.request.Request(snapshot, headers={'User-Agent': 'PassportWorldCam/2.0', 'Accept': 'image/*'})
        with urllib.request.urlopen(req, timeout=12) as response:
            raw = response.read(MAX_BYTES + 1)
        if len(raw) > MAX_BYTES:
            raise ValueError('Source image exceeds limit')
        with Image.open(BytesIO(raw)) as image:
            image.load()
            acquired = int(time.time())
            frame = encode_frame(image, acquired)
            full_frame = encode_frame(image, acquired, full=True)
            preview = BytesIO()
            ImageOps.contain(image.convert('RGB'), (768, 512)).save(preview, 'JPEG', quality=80)
        cached = (time.monotonic(), frame, full_frame, preview.getvalue(), acquired)
        with CACHE_LOCK:
            CACHE[camera_id] = cached
            CACHE.move_to_end(camera_id)
            while len(CACHE) > 64: CACHE.popitem(last=False)
        return (full_frame if full else frame, cached[3], acquired)


class Handler(BaseHTTPRequestHandler):
    def respond(self, status, data, content_type, acquired=None):
        self.send_response(status)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        if acquired: self.send_header('X-Fetched-At', str(acquired))
        self.end_headers()
        self.wfile.write(data)

    def json(self, data, status=200):
        return self.respond(status, json.dumps(data, ensure_ascii=False).encode(), 'application/json; charset=utf-8')

    def do_GET(self):
        parsed = urllib.parse.urlsplit(self.path)
        query = urllib.parse.parse_qs(parsed.query)
        path = parsed.path
        if path == '/': return self.respond(200, (ROOT / 'index.html').read_bytes(), 'text/html; charset=utf-8')
        if path == '/health': return self.json({'ok': True})
        if path == '/api/stats':
            return self.json({'cameras': len(CAMERAS), 'locations': len(LOCATIONS), 'capitals': len(COVERAGE),
                              'countries': len({c['country_code'] for c in CAMERAS}),
                              'capitals_with_sources': sum(bool(c['camera_ids']) for c in COVERAGE),
                              'capitals_verified': sum(bool(c.get('verified_camera_ids')) for c in COVERAGE),
                              'verified_snapshots': sum(c.get('status') == 'snapshot_verified' for c in CAMERAS)})
        if path == '/api/capitals': return self.json(COVERAGE)
        if path == '/api/index': return self.respond(200, index_binary(), 'application/octet-stream')
        if path == '/world-map.svg': return self.respond(200, (ROOT / 'world-map.svg').read_bytes(), 'image/svg+xml')
        if path == '/api/cameras': return self.json(catalogue(query.get('q', [''])[0], query.get('region', ['All'])[0]))
        if path == '/api/locations':
            return self.json(locations(query.get('q', [''])[0], query.get('region', ['All'])[0], query.get('capitals', ['0'])[0] == '1'))
        if path == '/api/random':
            eligible = [i for i, loc in enumerate(LOCATIONS) if camera_usable(BY_ID.get(loc.get('camera_id')))]
            return self.json(location_info(random.choice(eligible))) if eligible else self.json({'error': 'No available cameras'}, 503)
        parts = path.strip('/').split('/')
        if len(parts) == 3 and parts[:2] == ['api', 'location']:
            try:
                i = int(parts[2])
                if not 0 <= i < len(LOCATIONS): raise ValueError()
                return self.json(location_info(i))
            except (ValueError, IndexError): return self.json({'error': 'Unknown location'}, 404)
        if len(parts) == 3 and parts[:2] in [['api', 'frame'], ['api', 'preview'], ['api', 'frame-location']]:
            camera_id = parts[2]
            if parts[1] == 'frame-location':
                try:
                    i = int(camera_id)
                    if not 0 <= i < len(LOCATIONS): raise ValueError()
                    camera_id = LOCATIONS[i].get('camera_id')
                except (ValueError, IndexError): camera_id = None
            if camera_id not in BY_ID: return self.json({'error': 'No verified camera for this location'}, 404)
            try:
                frame, preview, acquired = fetch_frame(camera_id, full=query.get('full', ['0'])[0] == '1')
                return self.respond(200, preview if parts[1] == 'preview' else frame,
                                    'image/jpeg' if parts[1] == 'preview' else 'application/octet-stream', acquired)
            except (OSError, ValueError, UnidentifiedImageError, Image.DecompressionBombError):
                return self.json({'error': 'Source unavailable; try its official page'}, 502)
        return self.json({'error': 'Not found'}, 404)

    def log_message(self, fmt, *args): pass


class Server(ThreadingHTTPServer):
    daemon_threads = True
    slots = threading.BoundedSemaphore(8)
    def process_request(self, request, client_address):
        self.slots.acquire()
        try: super().process_request(request, client_address)
        except BaseException:
            self.slots.release()
            raise
    def process_request_thread(self, request, client_address):
        try: super().process_request_thread(request, client_address)
        finally: self.slots.release()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=8787)
    parser.add_argument('--check-sources', action='store_true')
    parser.add_argument('--limit', type=int, default=0, help='Limit source check count; 0 checks all')
    args = parser.parse_args()
    if args.check_sources:
        def check(c):
            try:
                frame, _, _ = fetch_frame(c['id'])
                return c['id'], 'OK', len(frame)
            except Exception as e: return c['id'], type(e).__name__
        with ThreadPoolExecutor(max_workers=3) as pool:
            for result in pool.map(check, CAMERAS[:args.limit] if args.limit else CAMERAS): print(*result, flush=True)
    else:
        print(f'WorldCam gateway listening on {args.host}:{args.port}', flush=True)
        Server((args.host, args.port), Handler).serve_forever()
