#!/usr/bin/env python3
"""Conservative Q8 swept-Z broadphase regression; no device FPS implied."""
import random

def verify():
    rng = random.Random(170026)
    rejected = swept = 0
    for _ in range(250_000):
        ba, bb = rng.randrange(300, 5500), rng.randrange(300, 5500)
        ha, hb = rng.randrange(ba + 1), rng.randrange(bb + 1)
        dz = rng.randrange(40_000)
        va, vb = rng.randrange(-150_000, 150_000), rng.randrange(-150_000, 150_000)
        envelope = ba + bb + 24
        cull = dz > envelope and dz - envelope > abs(va-vb)//30
        old_overlap_z = dz < envelope
        # Old swept-disc branch requires positive gap, approaching speed,
        # and vertical closure within one fixed 30 Hz step.
        gap = dz - ha - hb
        closing = va - vb
        swept_candidate = gap > 0 and closing > 0 and closing//30 >= gap
        if cull:
            rejected += 1
            assert not old_overlap_z and not swept_candidate
        if swept_candidate:
            swept += 1
            assert not cull
    assert rejected == 192802 and swept == 10352
    print(f"Q8 swept vertical broadphase: 250000 cases PASS; {rejected} far pairs rejected; {swept} fast swept pairs retained")

if __name__ == "__main__":
    verify()
