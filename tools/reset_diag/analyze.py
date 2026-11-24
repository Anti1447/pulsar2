#!/usr/bin/env python3
"""Tables for docs/GCO_RESET_MIX.md from a ResetDiag output folder (stdlib only).

Usage: python3 tools/reset_diag/analyze.py <outDir>
"""
import csv, math, sys
from collections import defaultdict

out = sys.argv[1]
GAMMA, RET_STD, DT = 0.9969, None, 8 / 120.0
# live mix (MakeEnv): BallNearCar .20, AirDrill .20 (soloFrac .5), AirPlay .30 (wall .30, the rest half flip / half carry), Kickoff .10, Random .20
W = {"BallNearCar": .20, "AirDrill_solo": .10, "AirDrill_contest": .10, "AirPlay_wall": .09, "AirPlay_flip": .105, "AirPlay_carry": .105,
     "Kickoff": .10, "Random": .20}
CELLS = list(W)

def ci(p, n):
    return 196 * math.sqrt(max(p * (1 - p), 1e-9) / max(n, 1))

eps = defaultdict(list)
for r in csv.DictReader(open(f"{out}/episodes.csv")):
    eps[(r["cell"], int(r["mode"]))].append(r)

crit = {}
for r in csv.DictReader(open(f"{out}/critic.csv")):
    crit[(r["cell"], int(r["mode"]))] = [float(r[k]) for k in ("n", "sumG", "sumG2", "sumV", "sumV2", "sumGV")]

def critic_stats(m, std):
    n, sg, sg2, sv, sv2, sgv = m
    if n < 2: return (float("nan"),) * 3
    varg = sg2 / n - (sg / n) ** 2
    # V is in critic units (returns / return std)
    mse = (sg2 - 2 * std * sgv + std * std * sv2) / n
    cov = sgv / n - sg / n * sv / n
    varv = sv2 / n - (sv / n) ** 2
    return 1 - mse / varg, cov / math.sqrt(max(varg * varv, 1e-12)), sg2 / n

import json, os
std = 0.554
print(f"return std used for critic units: {std}\n")

print("## Per setter x mode")
print("| setter | mode | n | scored-by-blue % | scored-by-orange % | timeout % | cap % | goal<=20s % | mean s | median s | first touch s | no touch at all % | closer team wins % (of goals) | mean abs V0 (raw) | higher-V0 team wins % | critic EV | critic corr | E[G^2] per row |")
print("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
summary = {}
for cell in CELLS:
    for mode in (1, 2, 3):
        E = eps.get((cell, mode), [])
        n = len(E)
        if not n: continue
        oc = [int(e["outcome"]) for e in E]
        steps = sorted(int(e["steps"]) for e in E)
        b, o, t, c = (oc.count(k) / n for k in range(4))
        g20 = sum(1 for e in E if int(e["outcome"]) < 2 and int(e["steps"]) * DT <= 20) / n
        ft = [int(e["first_touch_step"]) * DT for e in E if int(e["first_touch_step"]) >= 0]
        notouch = 1 - len(ft) / n
        goals = [e for e in E if int(e["outcome"]) < 2 and int(e["blue_closer"]) != 0]
        cw = sum(1 for e in goals if (int(e["blue_closer"]) == 1) == (int(e["outcome"]) == 0)) / max(1, len(goals))
        v0 = [(float(e["v0_blue"]) * std, float(e["v0_orange"]) * std) for e in E]
        absv0 = sum(abs(a) + abs(b_) for a, b_ in v0) / (2 * n)
        gl = [e for e in E if int(e["outcome"]) < 2]
        vw = sum(1 for e in gl if (float(e["v0_blue"]) > float(e["v0_orange"])) == (int(e["outcome"]) == 0)) / max(1, len(gl))
        ev, corr, eg2 = critic_stats(crit[(cell, mode)], std)
        mean_s = sum(steps) / n * DT
        summary[(cell, mode)] = dict(n=n, b=b, o=o, t=t, c=c, g20=g20, mean_steps=sum(steps) / n, ev=ev, eg2=eg2, absv0=absv0, vw=vw, rows=crit[(cell, mode)][0])
        print(f"| {cell} | {mode}v{mode} | {n} | {100*b:.0f} ±{ci(b,n):.0f} | {100*o:.0f} | {100*t:.0f} ±{ci(t,n):.0f} | {100*c:.0f} | {100*g20:.0f} | {mean_s:.1f} | {steps[n//2]*DT:.1f} | "
              f"{(sum(ft)/len(ft) if ft else float('nan')):.1f} | {100*notouch:.0f} | {100*cw:.0f} | {absv0:.2f} | {100*vw:.0f} | {ev:.2f} | {corr:.2f} | {eg2:.3f} |")

print("\n## Share of training rows and of return variance, by setter (per mode, at the live weights)")
print("| setter | reset weight | " + " | ".join(f"{m}v{m} row share % | {m}v{m} sum-G^2 share %" for m in (1, 2, 3)) + " |")
print("|---|---|" + "---|" * 6)
tot = {m: (sum(W[c] * summary[(c, m)]["mean_steps"] for c in CELLS if (c, m) in summary),
           sum(W[c] * summary[(c, m)]["mean_steps"] * summary[(c, m)]["eg2"] for c in CELLS if (c, m) in summary)) for m in (1, 2, 3)}
for c in CELLS:
    cells_ = []
    for m in (1, 2, 3):
        s = summary.get((c, m))
        if not s: cells_ += ["", ""]; continue
        cells_ += [f"{100*W[c]*s['mean_steps']/tot[m][0]:.0f}", f"{100*W[c]*s['mean_steps']*s['eg2']/tot[m][1]:.0f}"]
    print(f"| {c} | {W[c]:.3f} | " + " | ".join(cells_) + " |")

# ---------------- match summary ----------------
if os.path.exists(f"{out}/match.csv"):
    print("\n## Self-play matches from kickoffs (goal-only terminals, no timeout)")
    print("| mode | arenas | minutes | goals / min | seconds per goal | 20 s no-touch stalls / min | kickoff first touch s |")
    print("|---|---|---|---|---|---|---|")
    M = defaultdict(list)
    for r in csv.DictReader(open(f"{out}/match.csv")): M[int(r["mode"])].append(r)
    match_minutes = None
    for m, rows in sorted(M.items()):
        goals = sum(int(r["goals_blue"]) + int(r["goals_orange"]) for r in rows)
        stalls = sum(int(r["stall20_count"]) for r in rows)
        kt = sum(float(r["kickoff_first_touch_s_sum"]) for r in rows) / max(1, sum(int(r["kickoff_touched"]) for r in rows))
        mins = float(sys.argv[2]) / 60 * len(rows) if len(sys.argv) > 2 else 5.0 * len(rows)
        print(f"| {m}v{m} | {len(rows)} | {mins:.0f} | {goals/mins:.2f} | {60*mins/max(1,goals):.1f} | {stalls/mins:.2f} | {kt:.2f} |")

# ---------------- coverage ----------------
def bins_of(r):
    team = int(r["team"]); s = 1 if team == 0 else -1  # canonical: own goal at -y
    by, bz = s * float(r["by"]), float(r["bz"])
    bx = float(r["bx"])
    bspeed = math.sqrt(float(r["bvx"]) ** 2 + float(r["bvy"]) ** 2 + float(r["bvz"]) ** 2)
    cy, cz = s * float(r["cy"]), float(r["cz"])
    cx = float(r["cx"])
    boost, og, cs = float(r["boost"]), int(r["onground"]), float(r["cspeed"])
    d = math.sqrt((cx - bx) ** 2 + (cy - by) ** 2 + (cz - bz) ** 2)
    yield "ball third", "defensive" if by < -1707 else ("offensive" if by > 1707 else "midfield")
    yield "ball height", "ground <150" if bz < 150 else ("150-600" if bz < 600 else ("600-1200" if bz < 1200 else ">1200"))
    yield "ball speed", "<300" if bspeed < 300 else ("300-1200" if bspeed < 1200 else ("1200-2500" if bspeed < 2500 else ">2500"))
    yield "ball near wall", "side wall |x|>3600" if abs(bx) > 3600 else ("back wall |y|>4600" if abs(by) > 4600 else "open field")
    yield "car boost", "0-10" if boost < 10 else ("10-33" if boost < 33 else ("33-66" if boost < 66 else "66-100"))
    yield "car", "on ground" if og else ("air z<300" if cz < 300 else "air z>=300")
    yield "car speed", "<500" if cs < 500 else ("500-1400" if cs < 1400 else ("1400-2200" if cs < 2200 else "supersonic"))
    yield "car vs ball", "goal-side of ball" if cy < by else "upfield of ball (beaten)"
    yield "car-ball distance", "<500" if d < 500 else ("500-1500" if d < 1500 else ("1500-3500" if d < 3500 else ">3500"))
    yield "car position", "own third" if cy < -1707 else ("opp third" if cy > 1707 else "midfield")

if os.path.exists(f"{out}/samples.csv"):
    neps = {k: len(v) for k, v in eps.items()}
    H = {ph: {m: defaultdict(float) for m in (1, 2, 3)} for ph in ("reset0", "reset", "match")}
    T = {ph: {m: 0.0 for m in (1, 2, 3)} for ph in H}
    for r in csv.DictReader(open(f"{out}/samples.csv")):
        ph, m = r["phase"], int(r["mode"])
        w = 1.0 if ph == "match" else W[r["cell"]] / max(1, neps.get((r["cell"], m), 1))
        T[ph][m] += w
        for k in bins_of(r):
            H[ph][m][k] += w
    print("\n## Coverage: share of car-states (%) in each bin. reset0 = the states the setters emit, reset = every state visited in training episodes "
          "(mix-weighted), match = self-play from kickoffs")
    for m in (1, 2, 3):
        print(f"\n### {m}v{m}")
        print("| feature | bin | reset0 % | reset (visited) % | match % | visited / match |")
        print("|---|---|---|---|---|---|")
        keys = sorted(set(H["match"][m]) | set(H["reset"][m]), key=lambda k: k[0])
        order = []
        for k in keys:
            if k[0] not in order: order.append(k[0])
        for f in order:
            for k in [k for k in keys if k[0] == f]:
                a = 100 * H["reset0"][m][k] / max(T["reset0"][m], 1e-9)
                b_ = 100 * H["reset"][m][k] / max(T["reset"][m], 1e-9)
                c_ = 100 * H["match"][m][k] / max(T["match"][m], 1e-9)
                ratio = b_ / c_ if c_ > 0 else float("inf")
                flag = " **under**" if ratio < 0.6 and c_ >= 2 else (" **over**" if ratio > 1.7 and b_ >= 2 else "")
                print(f"| {k[0]} | {k[1]} | {a:.1f} | {b_:.1f} | {c_:.1f} | {ratio:.2f}{flag} |")
