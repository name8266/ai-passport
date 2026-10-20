#!/usr/bin/env python3
"""Deterministic 10,000-path calibration; no third-party Python packages."""
import csv
import json
from pathlib import Path
from statistics import mean

root = Path(__file__).resolve().parent.parent
rows = list(csv.DictReader((root / 'build/brake-hero/trajectories.csv').open()))
assert len(rows) == 10000
result = {'seed': '0x42524831', 'track_m': 500, 'trajectories': len(rows), 'strategies': {}}
for strategy in dict.fromkeys(r['strategy'] for r in rows):
    group = [r for r in rows if r['strategy'] == strategy]
    peaks = [float(r['peak_kmh']) for r in group]
    stopped = sorted(float(r['remaining_m']) for r in group if r['success'] == '1')
    result['strategies'][strategy] = {
        'count': len(group), 'success_rate': mean(int(r['success']) for r in group),
        'qualified_rate': mean(int(r['qualified']) for r in group),
        'brake_failure_rate': mean(int(r['brake_failed']) for r in group),
        'peak_kmh': {'min': min(peaks), 'mean': mean(peaks), 'max': max(peaks)},
        'stop_remaining_m': {q: stopped[round((len(stopped)-1)*p)] if stopped else None
                             for q, p in [('p05', .05), ('p50', .5), ('p95', .95)]},
    }
assert result['strategies']['conservative']['qualified_rate'] > .9
assert result['strategies']['late_brake']['success_rate'] < .95
text = json.dumps(result, indent=2)
(root / 'build/brake-hero/simulation.json').write_text(text+'\n')
print(text)
