#!/usr/bin/env python3
"""Accept/reject a real ESP32-C3 BENCH log; never infer hardware FPS."""
import argparse
import re
import sys

FPS = re.compile(r"\bDENSE_FRAME\s+([\d.]+) Hz\s+bodies>=150\s+active_s=([\d.]+)\s+presented=(\d+)")
COST = re.compile(r"\bPERF_MEAN\s+.*?dense_physics_steps=(\d+)\s+dense_physics_us_per_tick=(\d+)\s+dense_draw_calls=(\d+)\s+dense_draw_us_per_call=(\d+)")
STATUS = re.compile(r"\bSTATUS\b.*?\boverflow=(\d+)\s+valid=([01])")

def analyze(content, min_fps=20., min_seconds=60.):
    windows, costs, statuses = [], [], []
    for line in content.splitlines():
        f=FPS.search(line)
        if f:
            fps, seconds, frames = float(f[1]), float(f[2]), int(f[3])
            if seconds>0:windows.append((fps,seconds,frames))
        c=COST.search(line)
        if c:costs.append(tuple(map(int,c.groups())))
        st=STATUS.search(line)
        if st:statuses.append((int(st[1]),int(st[2])))
    seconds=sum(w[1] for w in windows)
    frames=sum(w[2] for w in windows)
    observed=frames/seconds if seconds else 0
    minimum=min((w[0] for w in windows),default=0)
    physics_steps=sum(c[0] for c in costs)
    draw_calls=sum(c[2] for c in costs)
    physics=sum(c[0]*c[1] for c in costs)/physics_steps if physics_steps else 0
    draw=sum(c[2]*c[3] for c in costs)/draw_calls if draw_calls else 0
    # At 20 FPS, advancing 30 Hz physics requires 1.5 steps per frame.
    required_work=1.5*physics+draw
    problems=[]
    if seconds<min_seconds:problems.append(f"only {seconds:.1f}s observed; need {min_seconds:g}s")
    if not windows:problems.append("missing DENSE_FRAME measurements")
    if minimum<min_fps:problems.append(f"worst 10s window {minimum:.1f} FPS < {min_fps:g}")
    if not costs:problems.append("missing PERF_MEAN component timings")
    if not statuses:problems.append("missing STATUS valid/overflow checks")
    if any(n!=0 or good!=1 for n,good in statuses):problems.append("overflow or invalid rigid-body state")
    if physics>33333:problems.append("average physics tick exceeds 30 Hz budget")
    return {
        "passed":not problems,"problems":problems,"seconds":seconds,"frames":frames,
        "measured_fps":observed,"slowest_window_fps":minimum,
        "physics_us_per_tick":physics,"draw_us_per_call":draw,
        "approx_work_us_for_20fps":required_work,"windows":len(windows),"statuses":len(statuses)
    }

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("serial_log",help="complete serial output from BENCH, UTF-8")
    p.add_argument("--min-fps",type=float,default=20.)
    p.add_argument("--min-seconds",type=float,default=60.)
    a=p.parse_args(argv)
    with open(a.serial_log,encoding="utf-8",errors="replace") as f:
        result=analyze(f.read(),a.min_fps,a.min_seconds)
    print("PASS" if result["passed"] else "NOT ACCEPTED")
    print(f"Dense: {result['seconds']:.1f}s; frames={result['frames']}; avg={result['measured_fps']:.1f} FPS; slowest={result['slowest_window_fps']:.1f} FPS")
    print(f"physics={result['physics_us_per_tick']:.0f} us/tick; draw={result['draw_us_per_call']:.0f} us/call; CPU work model for 20 FPS={result['approx_work_us_for_20fps']:.0f} us/frame")
    for issue in result["problems"]:print("  -",issue)
    return 0 if result["passed"] else 1

if __name__=="__main__":sys.exit(main())
