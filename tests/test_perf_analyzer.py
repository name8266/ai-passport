#!/usr/bin/env python3
"""Strict regressions for 20-FPS serial performance gate."""
import importlib.util
from pathlib import Path

path = Path(__file__).resolve().parents[1] / "tools" / "analyze_pusher_perf.py"
spec = importlib.util.spec_from_file_location("perf_analyzer", path)
perf = importlib.util.module_from_spec(spec)
spec.loader.exec_module(perf)

def scenario(fps, physics, draw, overflow=0, valid=1):
    sample=(
        f"DENSE_FRAME {fps:.1f} Hz bodies>=150 active_s=10.000 "
        f"presented={int(10*fps)} interval_max_us=70000\n"
        f"PERF_MEAN physics_steps=300 physics_us_per_tick=15000 "
        f"draw_calls=200 draw_us_per_call=16000 "
        f"dense_physics_steps=300 dense_physics_us_per_tick={physics} "
        f"dense_draw_calls=200 dense_draw_us_per_call={draw}\n"
        f"STATUS bank=20 win=42 overflow={overflow} valid={valid} time=100\n"
    )
    return perf.analyze(sample*6)

normal=scenario(25,12000,13000)
assert normal["passed"] and normal["seconds"]==60.
slow=scenario(6,45000,13000)
assert not slow["passed"] and any("physics tick" in s for s in slow["problems"])
invalid=scenario(25,12000,13000,valid=0)
assert not invalid["passed"] and any("invalid" in s for s in invalid["problems"])
overflow=scenario(25,12000,13000,overflow=1)
assert not overflow["passed"]
short=perf.analyze("DENSE_FRAME 30.0 Hz bodies>=150 active_s=10.000 presented=300")
assert not short["passed"]
print("20-FPS serial acceptance analyzer (good/slow/invalid/overflow/incomplete): PASS")
