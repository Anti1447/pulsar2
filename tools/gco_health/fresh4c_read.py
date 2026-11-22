#!/usr/bin/env python3
"""Health read for the critic-heads fresh run (docs/GCO_CRITIC_HEADS_FRESH.md): last-100M window means vs fresh3 and run 1 at the same step. usage: fresh4_read.py"""
import json, statistics as st
B='/home/nhafe/pulsar2/build/'
def load(f):
    try: return [json.loads(l) for l in open(f)]
    except Exception: return []
r4=load(B+'checkpoints_gco_fresh4c/metrics.jsonl'); r3=load(B+'checkpoints_gco_fresh3/metrics.jsonl'); r1=load(B+'checkpoints_gco_fresh/metrics.jsonl')
if not r4: print('no metrics yet'); raise SystemExit
ts=r4[-1]['ts']
def win(rows,lo,hi,k):
    v=[r['m'][k] for r in rows if k in r['m'] and lo<=r['ts']<hi]; return st.mean(v) if v else None
def f(x): return '-' if x is None else f"{x:.4g}"
lo=max(0,ts-100e6)
print(f"fresh4 ts {ts/1e6:.0f}M rows {len(r4)} (window {lo/1e6:.0f}-{ts/1e6:.0f}M)")
pol=['Player/Ball Touch Ratio','Episode Length','GCO/Goals/10k Rows 1v1','Policy Entropy','GCO/Critic Explained Variance','Policy Update Magnitude','Overall Steps/Second','Gauntlet/Mean Win %','Gauntlet/Mean First Touch Us %']
print(f"{'POLICY GUARD':36s}{'fresh4':>12s}{'fresh3':>12s}{'run1':>12s}")
for k in pol: print(f"{k:36s}{f(win(r4,lo,ts+1,k)):>12s}{f(win(r3,lo,ts+1,k)):>12s}{f(win(r1,lo,ts+1,k)):>12s}")
print(f"{'HEADS':36s}{'window':>12s}")
keys=sorted({k for r in r4[-30:] for k in r['m'] if k.startswith('Heads/')})
for k in keys: print(f"{k:36s}{f(win(r4,lo,ts+1,k)):>12s}")
