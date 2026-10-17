#!/usr/bin/env python3
"""Deterministic work-count proxy; not a firmware or MCU benchmark."""
OLD_START, NEW_START = 256, 156
OLD_MAX, NEW_MAX = 288, 180
OLD_CONTACTS, NEW_CONTACTS = 2304, 2048

def points(n):
    """Simplified 10-column, 3-row, layered lower-bed arrangement."""
    out = []
    for i in range(n):
        layer, rem = divmod(i, 30)
        row, col = divmod(rem, 10)
        out.append((22+18*col+(8 if row&1 else 0), 100+16.5*row, 1.3+2.6*layer))
    return out

def operations(n):
    p = points(n)
    xy = close = 0
    for i in range(n):
        x,y,z = p[i]
        for j in range(i+1,n):
            xx,yy,zz = p[j]
            if abs(x-xx)<=16.1 and abs(y-yy)<=16.7:
                xy += 1
                if abs(z-zz)<=3.0:
                    close += 1
    return n*(n-1)//2, xy, close

def amdahl(affected_fraction, work_ratio):
    return 1/(1-affected_fraction + affected_fraction*work_ratio)

def report():
    a,b=operations(OLD_START),operations(NEW_START)
    assert a==(32640,3756,1117) and b==(12090,1335,631)
    print("Operation-count proxy only; no actual FPS measurements")
    print(f"Start: {OLD_START} -> {NEW_START}; capacity: {OLD_MAX} -> {NEW_MAX}")
    print(f"Contact storage: {OLD_CONTACTS*40//1024} -> {NEW_CONTACTS*40//1024} KiB")
    for title,old,new in zip(("all pairs","XY candidates","3D close pairs"),a,b):
        print(f"{title}: {old} -> {new}; work ratio={new/old:.4f}")
    linear=NEW_START/OLD_START
    quadratic=(NEW_START*(NEW_START-1))/(OLD_START*(OLD_START-1))
    for f in (.25,.50,.75):
        print(f"affected={f:.0%}: linear speedup={amdahl(f,linear):.3f}x; pair-bound={amdahl(f,quadratic):.3f}x")
    print("20 FPS hypothetical / 50% affected / 30 Hz cap:",
        round(min(30,20*amdahl(.5,linear)),1),
        "to", round(min(30,20*amdahl(.5,quadratic)),1), "FPS")

if __name__=="__main__":
    report()
