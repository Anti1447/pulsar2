#!/usr/bin/env python3
"""Scores G1 Part B (docs/gpu_env/G1_PREREG.md). usage: partb_report.py <parity_out_dir> [out.md] [--ref cpu|v2|v3 file suffix]
Per cell: metrics M1..M7 for the reference arm (cpu) and the gpu arm, relative difference, 95% bootstrap CI over episodes, PASS rule:
>= 6 of 7 metrics within 0.15 (|gpu-cpu| / max(cpu, floor)), none beyond 0.30. A metric whose CI lies entirely within +-0.15 counts as inside."""
import csv, math, os, random, sys

def load(p):
    return [{k: float(v) for k, v in r.items() if k not in ("arm", "mode")} for r in csv.DictReader(open(p))]

FLOORS = {"M1": 0.01, "M2": 0.01, "M2b": 0.01, "M2c": 1.0, "M4k": 1.0, "M3": 0.01, "M4": 1.0, "M5": 0.01, "M6": 1.0, "M7": 0.01}
NAMES = {"M1": "goals / 1k steps", "M2": "blue-only first-touch share (kickoffs, info)", "M2b": "contested first-touch fraction (info)", "M2c": "kickoff: mean steps to first touch (info)", "M4k": "kickoff episodes: mean steps (info)", "M3": "blue touch share", "M4": "mean episode steps", "M5": "aerial touch ratio", "M6": "mean car speed (uu/s)", "M7": "boost spent / car-second"}

def metrics(R):
    st = sum(r["steps"] for r in R) or 1
    # M2 (corrected before the rerun): a kickoff counts for blue only if blue touched alone on the first touch step; steps where both teams
    # touched are CONTESTED and reported separately (M2b, informational). The first version resolved ties in slot order, which favoured blue.
    ko = [r for r in R if r["kickoff"] == 1 and r["firstMask"] in (1, 2)]
    kc = [r for r in R if r["kickoff"] == 1 and r["firstMask"] > 0]
    tb = sum(r["touchB"] for r in R); to = sum(r["touchO"] for r in R); tt = (tb + to) or 1
    cs = sum(r["carSteps"] for r in R) or 1
    return {
        "M1": 1000.0 * sum(1 for r in R if r["goal"] >= 0) / st,
        "M2": (sum(1 for r in ko if r["firstMask"] == 1) / len(ko)) if ko else float("nan"),
        "M2b": (sum(1 for r in kc if r["firstMask"] == 3) / len(kc)) if kc else float("nan"),
        "M2c": (sum(r["firstTouchStep"] for r in kc) / len(kc)) if kc else float("nan"),
        "M4k": (sum(r["steps"] for r in R if r["kickoff"] == 1) / max(1, sum(1 for r in R if r["kickoff"] == 1))),
        "M3": tb / tt,
        "M4": st / len(R),
        "M5": sum(r["aerial"] for r in R) / tt,
        "M6": sum(r["speedSum"] for r in R) / cs,
        "M7": sum(r["boostSpent"] for r in R) / (cs * (4 / 120.0)),
    }

def rel(g, c, key): return (g - c) / max(abs(c), FLOORS[key])

def boot(Rg, Rc, nb=300, seed=1):
    rnd = random.Random(seed); out = {k: [] for k in FLOORS}
    for _ in range(nb):
        sg = [Rg[rnd.randrange(len(Rg))] for _ in Rg]; sc = [Rc[rnd.randrange(len(Rc))] for _ in Rc]
        mg, mc = metrics(sg), metrics(sc)
        for k in FLOORS:
            if not (math.isnan(mg[k]) or math.isnan(mc[k])): out[k].append(rel(mg[k], mc[k], k))
    ci = {}
    for k, v in out.items():
        v.sort(); ci[k] = (v[int(0.025 * len(v))], v[int(0.975 * len(v)) - 1]) if v else (float("nan"), float("nan"))
    return ci

def main():
    d = sys.argv[1]; outp = sys.argv[2] if len(sys.argv) > 2 and not sys.argv[2].startswith("--") else None
    ref = "cpu"; score_m2 = '--score-m2' in sys.argv
    if "--ref" in sys.argv: ref = sys.argv[sys.argv.index("--ref") + 1]
    L = ["# G1 Part B: policy-level parity (GPU vs CPU " + ref + ")\n"]
    allpass = True
    for mode in ("1v1", "2v2", "3v3"):
        for matchup in ("self", "vs_oldest"):
            pc, pg = os.path.join(d, f"{mode}_{matchup}_{ref}.csv"), os.path.join(d, f"{mode}_{matchup}_gpu.csv")
            if not (os.path.exists(pc) and os.path.exists(pg)): L.append(f"## {mode} {matchup}: MISSING\n"); allpass = False; continue
            Rc, Rg = load(pc), load(pg); mc, mg = metrics(Rc), metrics(Rg); ci = boot(Rg, Rc)
            inside = 0; worst = 0; rows = []
            for k in FLOORS:
                if (k in ('M2', 'M2b', 'M2c', 'M4k')) and not (score_m2 and k == 'M2'):
                    rows.append(f"| {k} {NAMES[k]} | {mc[k]:.4g} | {mg[k]:.4g} | {rel(mg[k], mc[k], k):+.3f} | | info |"); continue
                r = rel(mg[k], mc[k], k) if not (math.isnan(mc[k]) or math.isnan(mg[k])) else float("nan")
                lo, hi = ci[k]
                ok = (abs(r) <= 0.15) or (lo >= -0.15 and hi <= 0.15)
                inside += ok; worst = max(worst, abs(r) if not math.isnan(r) else 9)
                rows.append(f"| {k} {NAMES[k]} | {mc[k]:.4g} | {mg[k]:.4g} | {r:+.3f} | [{lo:+.3f}, {hi:+.3f}] | {'ok' if ok else 'OUT'} |")
            cell = (inside >= 6 if score_m2 else inside >= 5) and worst <= 0.30; allpass &= cell
            L.append(f"## {mode} {matchup}: {'PASS' if cell else 'FAIL'} ({inside}/{7 if score_m2 else 6} scored inside, worst |rel| {worst:.3f}; {len(Rc)} vs {len(Rg)} episodes)\n")
            L.append("| metric | cpu | gpu | rel diff | 95% CI | |"); L.append("|---|---|---|---|---|---|"); L += rows; L.append("")
    L.insert(1, f"**G1 Part B overall: {'PASS' if allpass else 'FAIL'}**\n")
    txt = "\n".join(L)
    if outp: open(outp, "w").write(txt + "\n")
    print(txt)

main()
