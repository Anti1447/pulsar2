#!/usr/bin/env python3
"""The 5 UU accuracy score: GPU vs RocketSim v3 position error of every moving car and the ball, sampled every 30 ticks
(0.25 s) over each whole scenario. An object counts at a sample only once it has moved more than 5 UU from its start in the
v3 trace (parked objects would inflate the score). usage: score5.py <traces_dir> [--ref v3] [--worst N]"""
import csv, glob, math, os, sys, collections
root = sys.argv[1]
ref = sys.argv[sys.argv.index("--ref") + 1] if "--ref" in sys.argv else "v3"
worst_n = int(sys.argv[sys.argv.index("--worst") + 1]) if "--worst" in sys.argv else 0
other = sys.argv[sys.argv.index("--arm") + 1] if "--arm" in sys.argv else "gpu"
def fam(n):
    p = n.split("_")
    if p[0] == "rand": return "rand_" + p[1]
    if p[0] in ("opp", "mate", "bump"): return "bumps"
    if p[0] in ("wall", "car"): return "walls"
    if p[0] == "ball" and "parked" not in n: return "ball"
    if p[0] in ("carhit", "aerialhit", "fliphit", "jumphit", "carry", "kickoff", "squeeze") or "parked" in n: return "hits"
    if p[0] in ("dodge",): return "dodges"
    return "driving"
T = collections.defaultdict(lambda: [0, 0, 0, 0])  # samples, passed, scenarios, scenarios fully passed
worst = []
for f in sorted(glob.glob(os.path.join(root, ref, "*.csv"))):
    name = os.path.basename(f)[:-4]
    g = os.path.join(root, other, name + ".csv")
    if not os.path.exists(g): continue
    C, G = list(csv.DictReader(open(f))), list(csv.DictReader(open(g)))
    objs = ["bp"] + [k[:-2] + "p" for k in C[0] if k.endswith("_px")]
    objs = ["bp"] + [f"c{i}_p" for i in range(sum(1 for k in C[0] if k.endswith("_px")))]
    ok = True; first = None; mx = 0
    for o in objs:
        p0 = [float(C[0][o + a]) for a in "xyz"]; moved = False
        for t in range(1, len(C)):
            pc = [float(C[t][o + a]) for a in "xyz"]
            moved = moved or math.dist(pc, p0) > 5
            if t % 30 or not moved: continue
            d = math.dist(pc, [float(G[t][o + a]) for a in "xyz"])
            T[fam(name)][0] += 1; T[fam(name)][1] += d < 5; mx = max(mx, d)
            if d >= 5:
                ok = False; first = (t, o) if first is None or t < first[0] else first
    T[fam(name)][2] += 1; T[fam(name)][3] += ok
    if not ok: worst.append((first[0], mx, name, first[1]))
tot = [sum(v[i] for v in T.values()) for i in range(4)]
print(f"{'family':12} {'samples':>8} {'<5uu':>7} {'scenarios all <5uu':>20}")
for k, v in sorted(T.items()): print(f"{k:12} {v[0]:8} {100*v[1]/max(v[0],1):6.1f}% {v[3]:9}/{v[2]}")
print(f"{'TOTAL':12} {tot[0]:8} {100*tot[1]/max(tot[0],1):6.2f}% {tot[3]:9}/{tot[2]}")
for t, mx, name, o in sorted(worst)[:worst_n]: print(f"  first fail tick {t:4} ({o}) max {mx:8.1f}  {name}")
