#!/usr/bin/env python3
"""Summarize actual board PERF logs; never manufacture device measurements."""
import argparse
import json
import re
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('log', type=Path)
args = parser.parse_args()
windows = []
for line in args.log.read_text(errors='replace').splitlines():
    if 'PERF refresh_fps=' in line:
        pairs = re.findall(r'(\w+)(?:=|<=)([\d.]+)', line)
        windows.append({k: float(v) for k, v in pairs})
if not windows:
    parser.error('No device PERF samples found. Device tests remain NOT RUN.')
result = {'source': str(args.log), 'sample_windows': len(windows),
          'minimum_refresh_fps': min(w['refresh_fps'] for w in windows),
          'maximum_frame_interval_us': max(w['interval_max_us'] for w in windows),
          'worst_window_p95_upper_ms': max(w['interval_p95_ms'] for w in windows),
          'minimum_internal_heap': min(w['internal_min'] for w in windows),
          'maximum_render_us': max(w['render_max_us'] for w in windows),
          'p95_overflow': any(w['interval_p95_ms'] >= 128 for w in windows),
          'maximum_audio_feed_gap_us': max(w.get('audio_max_gap_us',0) for w in windows),
          'note': 'Completed full-panel DMA frames in v2; physical appearance/audio still require observation.'}
print(json.dumps(result, indent=2))
