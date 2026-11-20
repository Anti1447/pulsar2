#!/usr/bin/env python3
"""Health read for fresh run 3 beside runs 1 and 2 at the same step (docs/GCO_FRESH3.md)."""
import json,statistics as st
B='/home/nhafe/pulsar2/build/'
def load(f):
    try: return [json.loads(l) for l in open(f)]
    except Exception: return []
r1=load(B+'checkpoints_gco_fresh/metrics.jsonl'); r2=load(B+'checkpoints_gco_fresh2/metrics.jsonl'); r3=load(B+'checkpoints_gco_fresh3/metrics.jsonl')
ts=r3[-1]['ts']; print(f"RUN3 ts {ts/1e6:.0f}M steps, iter {r3[-1]['iter']}, rows {len(r3)}")
def med(rows,k,lo,hi):
    v=[r['m'][k] for r in rows if k in r['m'] and lo<=r['ts']<=hi]; return st.median(v) if v else None
keys=['Player/Ball Touch Ratio','Player/Aerial Touch Ratio','Player/In Air Ratio','Player/Speed Towards Ball','GCO/Goals/10k Rows 1v1','Episode Length','Rewards/N4RLGC21NoTouchTimeoutPenaltyE','Policy Entropy','GCO/Critic Explained Variance','Policy Update Magnitude','Gauntlet/Mean Win %','Gauntlet/Mean First Touch Us %','Gauntlet/Nexto/Win %','Overall Steps/Second']
w=60e6
def f(x): return '-' if x is None else f"{x:.4g}"
print(f"{'panel':46s}{'run3 med':>11s}{'run3 last':>11s}{'run1 same':>11s}{'run2 same':>11s}")
for k in keys:
    v=[r['m'][k] for r in r3 if k in r['m']]
    if not v: print(f"{k:46s}{'(absent)':>11s}"); continue
    print(f"{k:46s}{f(med(r3,k,ts-w,ts)):>11s}{f(v[-1]):>11s}{f(med(r1,k,ts-w,ts)):>11s}{f(med(r2,k,ts-w,ts)):>11s}")
print("Headroom keys present (should be 0):",sum(1 for r in r3 if any(k.startswith('Headroom/Vdag') for k in r['m'])))
