#!/usr/bin/env python3
"""Score the off-window capture scenario (sim-sdl --outside 1).

The game asks for mouse capture while the real pointer is elsewhere (another
monitor), the pointer is then brought into the window and the mouse moved.
Per cycle the capture works when the game receives relative motion and the
compositor's pointer stays put (locked) while the mouse moves.

    analyze-capture.py RUN_DIR
"""
import sys
from analyze import parse_probe, parse_sim

def main(out):
    pts = parse_probe(f"{out}/probe.raw")
    sim = parse_sim(f"{out}/sim.out")
    entered = {e["cycle"]: e["t_us"] for e in sim if e["kind"] == "entered"}
    caps = {e["cycle"]: e for e in sim if e["kind"] == "capture"}
    rels = {e["cycle"]: e["t_us"] for e in sim if e["kind"] == "release"}
    ok_all = 0
    for c in sorted(caps):
        t0, t1 = entered[c], rels.get(c, entered[c] + 1_000_000)
        # settle: ignore the pointer's own move into the window (first 50 ms)
        base_t = t0 + 50_000
        before = [p for p in pts if p[0] <= base_t]
        if not before:
            drift = float("nan")
        else:
            x0, y0 = before[-1][1], before[-1][2]
            win = [p for p in pts if base_t < p[0] <= t1]
            drift = max([((p[1] - x0) ** 2 + (p[2] - y0) ** 2) ** 0.5 for p in win] or [0.0])
        ev = caps[c]["events"]
        good = ev > 100 and drift <= 3.0
        ok_all += good
        print(f"cycle {c:2d}  {'PASS' if good else 'FAIL'}  game_events={ev:5d}  pointer_drift={drift:7.1f} px")
    print(f"RESULT {ok_all}/{len(caps)} captured")

if __name__ == "__main__":
    main(sys.argv[1])
