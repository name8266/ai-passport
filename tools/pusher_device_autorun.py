#!/usr/bin/env python3
"""Bounded, explicitly invoked USB practice runner; never flashes or erases.

Requires pyserial. Frames use the application's length-delimited RGB565 capture.
Raw logs/frames stay in --out; do not commit them.
"""
import argparse
import json
import re
import time
from pathlib import Path
import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--rounds', type=int, default=3, choices=range(1, 31))
    parser.add_argument('--seconds', type=int, default=1200)
    parser.add_argument('--seed', type=int, help='Repeatable 90-second practice benchmark; requires --rounds 1')
    args = parser.parse_args()
    if not 1 <= args.seconds <= 3600:
        parser.error('--seconds must be 1..3600')
    if args.seed is not None and (args.rounds != 1 or not 1 <= args.seed <= 2147483647):
        parser.error('--seed requires one round and a positive signed 32-bit seed')
    args.out.mkdir(parents=True, exist_ok=True)
    port = serial.Serial(port=None, baudrate=115200, timeout=.06)
    port.port = args.port
    port.dtr = False
    port.rts = False
    log = bytearray()
    started = time.monotonic()
    screenshots = []
    marker = b'CP3D_FRAME 240 320 RGB565_LE\n'
    faults = re.compile(rb'panic|Guru Meditation|Invalid rigid|Stack protection|DMA completion timed|Task creation failed|allocation failed|PCM write failed', re.I)

    def read_for(seconds):
        until = time.monotonic() + seconds
        while time.monotonic() < until:
            data = port.read(8192)
            log.extend(data)
            if faults.search(data):
                raise RuntimeError('Runtime fault in device.log')

    def send(command):
        port.write((command + '\n').encode('ascii'))
        read_for(.12)

    def shot(number):
        port.write(b'shot\n')
        data = bytearray()
        until = time.monotonic() + 15
        while marker not in data and time.monotonic() < until:
            data.extend(port.read(8192))
        if marker not in data:
            log.extend(data)
            raise RuntimeError('Capture header missing')
        start = data.index(marker)
        log.extend(data[:start])
        start += len(marker)
        size = 240 * 320 * 2
        while len(data) < start + size and time.monotonic() < until:
            data.extend(port.read(16384))
        if len(data) < start + size:
            raise RuntimeError('Capture truncated')
        raw = data[start:start + size]
        path = args.out / ('device-%02d.ppm' % number)
        rgb = bytearray()
        for offset in range(0, size, 2):
            value = raw[offset] | raw[offset + 1] << 8
            rgb.extend(((value >> 11) * 255 // 31, ((value >> 5) & 63) * 255 // 63, (value & 31) * 255 // 31))
        path.write_bytes(b'P6\n240 320\n255\n' + rgb)
        log.extend(data[start + size:])
        screenshots.append(path.name)

    completed = False
    error = None
    try:
        port.open()
        read_for(.5)
        send(('bench %d' % args.seed) if args.seed is not None else ('autorun %d' % args.rounds))
        next_query, next_shot = 10, 18
        while time.monotonic() - started < args.seconds:
            read_for(.2)
            elapsed = time.monotonic() - started
            if elapsed >= next_query:
                send('status')
                (args.out / 'device-live.log').write_bytes(log)
                print('observing %.1fs' % elapsed, flush=True)
                next_query += 20
            if elapsed >= next_shot:
                shot(len(screenshots) + 1)
                next_shot += 90
            if b'AUTORUN complete' in log:
                completed = True
                shot(len(screenshots) + 1)
                break
        if not completed:
            send('autorun 0')
            # Pause only if the cabinet is actively playing.
            recent = bytes(log[-10000:])
            views = re.findall(rb'ROGUE view=(\d+)', recent)
            if views and views[-1] == b'0':
                send('pause')
        send('status')
    except Exception as exc:
        error = str(exc)
        if port.is_open:
            port.write(b'autorun 0\n')
        raise
    finally:
        port.close()
        (args.out / 'device.log').write_bytes(log)
        clean = re.sub(rb'\x1b\[[0-9;]*m', b'', bytes(log)).decode('utf-8', errors='replace')
        frames = [float(value) for value in re.findall(r'coin3d: FRAME ([\d.]+) Hz', clean)]
        results = re.findall(r'AUTORUN result (.*)', clean)
        (args.out / 'device-summary.json').write_text(json.dumps({
            'completed': completed, 'error': error, 'requested_rounds': args.rounds, 'benchmark_seed': args.seed,
            'results': results, 'seconds': round(time.monotonic() - started, 2),
            'frame_windows_hz': frames,
            'active_frame_windows_hz': [float(v) for v in re.findall(r'PLAY_FRAME ([\d.]+) Hz', clean)], 'screenshots': screenshots,
            'runtime_fault': bool(faults.search(bytes(log))),
        }, indent=2) + '\n')


if __name__ == '__main__':
    main()
