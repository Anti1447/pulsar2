#!/usr/bin/env python3
"""Part A bands from docs/gpu_env/G1_PREREG.md, evaluated GPU vs CPU v3 (primary) and GPU vs CPU v2. usage: bands.py <traces_dir> [out.md]"""
import csv, math, os, sys

def load(path):
    with open(path) as f:
        return [{k: float(v) for k, v in row.items()} for row in csv.DictReader(f)]
def vec(r, p, k): return [r[f"{p}{k}x"], r[f"{p}{k}y"], r[f"{p}{k}z"]]
def dist(a, b): return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))
def norm(a): return math.sqrt(sum(x * x for x in a))
def angle(a, b):
    d = sum(x * y for x, y in zip(a, b)) / (norm(a) * norm(b) + 1e-12)
    return math.degrees(math.acos(max(-1, min(1, d))))
def car(r, k): return vec(r, "c0_", k)
def at(R, t): return R[min(t, len(R) - 1)]
def posd(A, B, t): return dist(car(at(A, t), "p"), car(at(B, t), "p"))
def veld(A, B, t): return abs(norm(car(at(A, t), "v")) - norm(car(at(B, t), "v")))
def fwd(A, B, t): return angle([at(A, t)["c0_fx"], at(A, t)["c0_fy"], at(A, t)["c0_fz"]], [at(B, t)["c0_fx"], at(B, t)["c0_fy"], at(B, t)["c0_fz"]])
def rel(a, b, floor=1.0): return abs(a - b) / max(abs(b), floor)
def apex(R): return max(r["c0_pz"] for r in R) - 17.0
def flight(R):  # ticks airborne (first leave ground to first return)
    t0 = next((i for i, r in enumerate(R) if r["c0_ground"] < 0.5), None)
    if t0 is None: return 0
    t1 = next((i for i in range(t0, len(R)) if R[i]["c0_ground"] > 0.5), len(R))
    return t1 - t0
def hdisp(R, t): a, b = R[0], at(R, t); return math.hypot(b["c0_px"] - a["c0_px"], b["c0_py"] - a["c0_py"])
def bspeed(r): return norm(vec(r, "", "bv")) if False else math.sqrt(r["bvx"] ** 2 + r["bvy"] ** 2 + r["bvz"] ** 2)
def pickups(R):
    t = []; prev = R[0]["c0_boost"]
    for i, r in enumerate(R):
        if r["c0_boost"] > prev + 0.5: t.append(i)
        prev = r["c0_boost"]
    return t

def checks(name, G, C):
    """returns list of (label, value, limit, ok)"""
    out = []
    def chk(label, val, lim): out.append((label, val, lim, val <= lim))
    if name == "drive_ground":
        for t in (120, 240): chk(f"pos@{t}", posd(G, C, t), 15); chk(f"speed@{t}", veld(G, C, t), 15)
    elif name == "drive_boost":
        for t in (120, 240): chk(f"pos@{t}", posd(G, C, t), 40); chk(f"speed@{t}", veld(G, C, t), 40)
        cg, cc = 100 - G[-1]["c0_boost"], 100 - C[-1]["c0_boost"]; chk("boost used rel", rel(cg, cc), 0.02)
    elif name.startswith("steer_"):
        chk("final pos", posd(G, C, 240), 30); chk("heading deg", fwd(G, C, 240), 2)
    elif name == "handbrake_turn":
        chk("final pos", posd(G, C, 180), 40); chk("heading deg", fwd(G, C, 180), 5)
    elif name in ("jump_hold_024", "jump_tap_3"):
        chk("apex rel", rel(apex(G), apex(C)), 0.03); chk("flight rel", rel(flight(G), flight(C)), 0.03)
    elif name == "double_jump" or name.startswith("dodge_"):
        chk("apex rel", rel(apex(G), apex(C)), 0.05)
        dg, dc = hdisp(G, 135), hdisp(C, 135)  # 1 s after the second input at tick 15
        chk("hdisp@1s after input (rel or 20uu)", min(rel(dg, dc), abs(dg - dc) / 20.0 * 0.08), 0.08)
        chk("landing orient deg", fwd(G, C, len(C) - 1), 5)
    elif name.startswith("ball_") and "parked" not in name:
        chk("ball pos@2s", dist([at(G, 240)["bpx"], at(G, 240)["bpy"], at(G, 240)["bpz"]], [at(C, 240)["bpx"], at(C, 240)["bpy"], at(C, 240)["bpz"]]), 30)
        chk("ball speed@2s rel", rel(bspeed(at(G, 240)), bspeed(at(C, 240)), 50), 0.03)
    elif name.startswith("carhit_"):
        pg, pc = max(bspeed(r) for r in G), max(bspeed(r) for r in C); chk("peak ball speed rel", rel(pg, pc), 0.10)
        tc = max(range(len(C)), key=lambda i: bspeed(C[i])); a = at(G, tc), at(C, tc)
        chk("ball dir deg @CPU peak", angle([a[0]["bvx"], a[0]["bvy"], a[0]["bvz"]], [a[1]["bvx"], a[1]["bvy"], a[1]["bvz"]]), 5)
    elif name.startswith("bump_"):
        dg, dc = max(r.get("c1_demoed", 0) for r in G), max(r.get("c1_demoed", 0) for r in C)
        chk("same demo outcome (0 = same)", abs(dg - dc), 0.5)
        chk("post-contact car speed rel", rel(norm(vec(at(G, 199), "c1_", "v")), norm(vec(at(C, 199), "c1_", "v")), 50), 0.10)
    elif name.startswith("pad_"):
        pg, pc = pickups(G), pickups(C)
        chk("pickup count diff", abs(len(pg) - len(pc)), 0.5)
        if pg and pc: chk("first pickup tick diff", abs(pg[0] - pc[0]), 1.5)
        if len(pg) > 1 and len(pc) > 1: chk("respawn pickup tick diff", abs(pg[1] - pc[1]), 2.5)
        chk("boost gained diff", abs(max(r["c0_boost"] for r in G) - max(r["c0_boost"] for r in C)), 1.0)
    return out

def main():
    root = sys.argv[1]; outp = sys.argv[2] if len(sys.argv) > 2 else None
    names = sorted(f[:-4] for f in os.listdir(os.path.join(root, "gpu")) if f.endswith(".csv"))
    L = ["# Part A band results (GPU vs CPU)\n", "PASS = every band holds. Primary reference: v3; second reference: v2.\n", "| scenario | vs v3 | vs v2 | failing bands vs v3 (value > limit) |", "|---|---|---|---|"]
    tot = {"v3": [0, 0], "v2": [0, 0]}; groups = {}
    for n in names:
        G = load(os.path.join(root, "gpu", n + ".csv")); res = {}
        for e in ("v3", "v2"):
            C = load(os.path.join(root, e, n + ".csv")); res[e] = checks(n, G, C)
        if not res["v3"]: continue
        ok3 = all(c[3] for c in res["v3"]); ok2 = all(c[3] for c in res["v2"])
        tot["v3"][0] += ok3; tot["v3"][1] += 1; tot["v2"][0] += ok2; tot["v2"][1] += 1
        bad = "; ".join(f"{c[0]} {c[1]:.3g}>{c[2]}" for c in res["v3"] if not c[3])
        L.append(f"| {n} | {'PASS' if ok3 else 'FAIL'} | {'PASS' if ok2 else 'FAIL'} | {bad} |")
    L.append(f"\nTotals: vs v3 {tot['v3'][0]}/{tot['v3'][1]} pass, vs v2 {tot['v2'][0]}/{tot['v2'][1]} pass.")
    txt = "\n".join(L)
    if outp: open(outp, "w").write(txt + "\n")
    print(txt)
main()
