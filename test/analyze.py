#!/usr/bin/env python3
"""Join the simulator timeline with KWin's real cursor trace.

usage: analyze.py OUTDIR [--tol PX] [--json]

For each release the simulator reports the global target T (the position the
game asked for). The KWin probe reports where the compositor actually puts the
cursor. A release PASSES when, with the mouse still at release time, the
compositor cursor is at T (within --tol px) soon after and stays there; with
motion after release (post_ms) it must start from T and track the injected
deltas (no teleport).
"""
import re, sys, json, bisect

def parse_probe(path):
    pts = []  # (t_us, x, y)
    for line in open(path, errors="replace"):
        m = re.search(r'string "(\d+) (-?[\d.]+) (-?[\d.]+) [ts]"', line)
        if m:
            pts.append((int(m.group(1)) * 1000, float(m.group(2)), float(m.group(3))))
    return pts

def parse_sim(path):
    ev = []
    for line in open(path):
        if not line.startswith("SIM "):
            continue
        f = line.split()
        d = {"kind": f[1]}
        for kv in f[2:]:
            if "=" in kv:
                k, v = kv.split("=", 1)
                try:
                    d[k] = int(v)
                except ValueError:
                    d[k] = v
        ev.append(d)
    return ev

def parse_mouse(path):
    ev = []
    try:
        for line in open(path):
            f = line.split()
            if len(f) == 4:
                ev.append((int(f[0]), f[1], float(f[2]), float(f[3])))
    except FileNotFoundError:
        pass
    return ev

def pos_at(pts, times, t):
    i = bisect.bisect_right(times, t) - 1
    return (pts[i][1], pts[i][2]) if i >= 0 else None

def analyze(out, tol=3.0, settle_window_us=400_000, lag_ms=20.0):
    pts = parse_probe(f"{out}/probe.raw")
    times = [p[0] for p in pts]
    sim = parse_sim(f"{out}/sim.out")
    mouse = parse_mouse(f"{out}/mouse.log")
    rels = [e for e in sim if e["kind"] == "release"]
    res = []
    for r in rels:
        t0, T = r["t_us"], (r["tx"], r["ty"])
        # motion injected after the release instant (the user keeps moving)
        # +-2 events of slack covers events straddling the release
        after = [m for m in mouse if m[1] == "rel" and m[0] > t0]
        win_end = t0 + settle_window_us
        # the simulator repositions the pointer (abs) to start the next cycle;
        # that is the harness moving it, not the system under test
        nxt = [m[0] for m in mouse if m[1] == "abs" and m[0] > t0]
        if nxt:
            win_end = min(win_end, nxt[0] - 1_000)
        samples = [p for p in pts if t0 - 5_000 <= p[0] <= win_end]
        def err_to_T(p, cum=(0.0, 0.0)):
            return ((p[1] - (T[0] + cum[0])) ** 2 + (p[2] - (T[1] + cum[1])) ** 2) ** 0.5
        # cumulative injected motion up to time t (after release)
        cum_t = []
        sx = sy = 0.0
        for m in after:
            sx += m[2]; sy += m[3]
            cum_t.append((m[0], sx, sy))
        ctimes = [c[0] for c in cum_t]
        def cum_at(t):
            i = bisect.bisect_right(ctimes, t) - 1
            return (cum_t[i][1], cum_t[i][2]) if i >= 0 else (0.0, 0.0)
        # expected track tolerance: a few events of slack
        slack = 0.0
        if after:
            step = max(abs(after[0][2]), abs(after[0][3]), 1.0)
            # the compositor applies the pointer's position one frame late
            # while the lock is being torn down, so the track may trail the
            # ideal by up to lag_ms of motion (events per ms * step)
            per_ms = len([m for m in after if m[0] < t0 + 50_000]) / 50.0
            slack = 3.0 * step + lag_ms * per_ms * step
        first_ok = None
        worst = 0.0
        bad_after_ok = 0
        for p in samples:
            if p[0] < t0:
                continue
            e = err_to_T(p, cum_at(p[0]))
            ok = e <= tol + slack
            if ok and first_ok is None:
                first_ok = p[0] - t0
            if first_ok is not None and not ok:
                bad_after_ok += 1
            worst = max(worst, e)
        final = pos_at(pts, times, win_end)
        final_cum = cum_at(win_end)
        final_err = None
        if final:
            final_err = ((final[0] - (T[0] + final_cum[0])) ** 2 +
                         (final[1] - (T[1] + final_cum[1])) ** 2) ** 0.5
        ok = (first_ok is not None and bad_after_ok == 0 and final_err is not None
              and final_err <= tol + slack)
        res.append({
            "cycle": r["cycle"], "target": T, "t_rel_us": t0,
            "settle_ms": None if first_ok is None else round(first_ok / 1000, 1),
            "final": final, "final_err_px": None if final_err is None else round(final_err, 1),
            "worst_err_px": round(worst, 1), "left_after_ok": bad_after_ok,
            "pass": bool(ok),
        })
    return res

if __name__ == "__main__":
    out = sys.argv[1]
    tol = 3.0
    if "--tol" in sys.argv:
        tol = float(sys.argv[sys.argv.index("--tol") + 1])
    res = analyze(out, tol)
    if "--json" in sys.argv:
        print(json.dumps(res))
    else:
        for r in res:
            print("cycle %2d  %s  settle=%s ms  final_err=%s px  worst=%s px  left_ok=%d  final=%s target=%s"
                  % (r["cycle"], "PASS" if r["pass"] else "FAIL", r["settle_ms"], r["final_err_px"],
                     r["worst_err_px"], r["left_after_ok"], r["final"], r["target"]))
        n = sum(1 for r in res if r["pass"])
        print("RESULT %d/%d passed" % (n, len(res)))
