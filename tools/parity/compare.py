#!/usr/bin/env python3
"""Three-way comparison of parity traces (gpu / v2 / v3). usage: compare.py <traces_dir> [out.md]
Per scenario and pair: error of car 0 / ball at fixed times, plus derived stats (peak ball speed, car apex, first-hit tick)."""
import csv, math, os, sys

def load(path):
    with open(path) as f:
        r = csv.DictReader(f)
        rows = [{k: float(v) for k, v in row.items()} for row in r]
    return rows

def d3(a, b, ka, kb=None):
    kb = kb or ka
    return math.sqrt(sum((a[ka[i]] - b[kb[i]]) ** 2 for i in range(3)))

def ang(a, b, p):  # angle between forward vectors, degrees
    fa = [a[f"{p}fx"], a[f"{p}fy"], a[f"{p}fz"]]; fb = [b[f"{p}fx"], b[f"{p}fy"], b[f"{p}fz"]]
    dot = sum(x * y for x, y in zip(fa, fb)); na = math.sqrt(sum(x * x for x in fa)); nb = math.sqrt(sum(x * x for x in fb))
    return math.degrees(math.acos(max(-1.0, min(1.0, dot / (na * nb + 1e-12)))))

def err_at(A, B, t, car=0):
    t = min(t, len(A) - 1, len(B) - 1)
    a, b = A[t], B[t]
    p = f"c{car}_"
    return {
        "car_pos": d3(a, b, [p + "px", p + "py", p + "pz"]),
        "car_vel": d3(a, b, [p + "vx", p + "vy", p + "vz"]),
        "fwd_deg": ang(a, b, p),
        "ball_pos": d3(a, b, ["bpx", "bpy", "bpz"]),
        "ball_vel": d3(a, b, ["bvx", "bvy", "bvz"]),
    }

def stats(R):
    bs = [math.sqrt(r["bvx"] ** 2 + r["bvy"] ** 2 + r["bvz"] ** 2) for r in R]
    cz = [r["c0_pz"] for r in R]; bz = [r["bpz"] for r in R]
    hit = next((i for i, r in enumerate(R) if r["c0_hit"] >= 0), -1)
    return {"peak_ball_speed": max(bs), "car_apex_z": max(cz), "ball_apex_z": max(bz), "first_hit_row": hit,
            "end_ball_speed": bs[-1], "end_boost": R[-1]["c0_boost"],
            "demoed": max(r.get("c1_demoed", 0) for r in R)}

def main():
    root = sys.argv[1]; out = sys.argv[2] if len(sys.argv) > 2 else None
    names = sorted(f[:-4] for f in os.listdir(os.path.join(root, "gpu")) if f.endswith(".csv"))
    lines = []
    P = lambda s="": lines.append(s)
    P("# Parity traces: GPU vs v2 vs v3 (car 0 and ball)")
    P("Errors at 1 s (tick 120) and 2 s (tick 240) in uu, uu/s, degrees; pairs: g-v2 = GPU vs CPU v2, g-v3 = GPU vs CPU v3, v2-v3 = the known engine difference.\n")
    P("| scenario | pair | car pos @1s | @2s | car vel @1s | @2s | fwd deg @2s | ball pos @1s | @2s | ball vel @2s |")
    P("|---|---|---|---|---|---|---|---|---|---|")
    D = {}
    for n in names:
        D[n] = {e: load(os.path.join(root, e, n + ".csv")) for e in ("gpu", "v2", "v3")}
        for pair in (("gpu", "v2"), ("gpu", "v3"), ("v2", "v3")):
            A, B = D[n][pair[0]], D[n][pair[1]]
            e1, e2 = err_at(A, B, 120), err_at(A, B, 240)
            P(f"| {n} | {pair[0]}-{pair[1]} | {e1['car_pos']:.1f} | {e2['car_pos']:.1f} | {e1['car_vel']:.1f} | {e2['car_vel']:.1f} | {e2['fwd_deg']:.2f} | {e1['ball_pos']:.1f} | {e2['ball_pos']:.1f} | {e2['ball_vel']:.1f} |")
    P("\n## Derived statistics\n")
    P("| scenario | engine | peak ball speed | car apex z | ball apex z | first hit row | end ball speed | end boost | victim demoed |")
    P("|---|---|---|---|---|---|---|---|---|")
    for n in names:
        for e in ("gpu", "v2", "v3"):
            s = stats(D[n][e])
            P(f"| {n} | {e} | {s['peak_ball_speed']:.1f} | {s['car_apex_z']:.1f} | {s['ball_apex_z']:.1f} | {s['first_hit_row']} | {s['end_ball_speed']:.1f} | {s['end_boost']:.1f} | {int(s['demoed'])} |")
    txt = "\n".join(lines)
    if out:
        open(out, "w").write(txt + "\n")
    print(txt)

main()
