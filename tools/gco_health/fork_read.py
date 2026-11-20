#!/usr/bin/env python3
"""Health read for a post-wall fork arm vs the control arm and run 1's 4.5-5.0B window (docs/GCO_POSTWALL_FORKS.md). usage: fork_read.py ctrl|kick|opp"""
import json,sys,statistics as st
arm=sys.argv[1]
B='/home/nhafe/pulsar2/build/'
def load(f):
    try: return [json.loads(l) for l in open(f)]
    except Exception: return []
ra=load(B+f'checkpoints_gco_fork_{arm}/metrics.jsonl'); rc=load(B+'checkpoints_gco_fork_ctrl/metrics.jsonl'); r1=load(B+'checkpoints_gco_fresh/metrics.jsonl')
START=4995887104
if not ra: print(f"fork {arm}: no metrics yet"); sys.exit()
ts=ra[-1]['ts']; done=ts-START
print(f"FORK {arm}: ts {ts/1e6:.0f}M (+{done/1e6:.0f}M of 500M), iter {ra[-1]['iter']}, rows {len(ra)}")
def win(rows,lo,hi,k):
    v=[r['m'][k] for r in rows if k in r['m'] and lo<=r['ts']<hi]; return st.mean(v) if v else None
def f(x): return '-' if x is None else f"{x:.4g}"
keys=['Gauntlet/Mean Win %','Gauntlet/Mean First Touch Us %','Gauntlet/Nexto/Win %','Gauntlet/GalactusV2/Win %','Gauntlet/ChronosV2/Win %','Gauntlet/Dice/Win %','GCO/Goals/10k Rows 1v1','Player/Ball Touch Ratio','Player/Aerial Touch Ratio','Player/In Air Ratio','Policy Entropy','GCO/Critic Explained Variance','Episode Length','Policy Update Magnitude','Overall Steps/Second']
print(f"{'panel':36s}{'arm since start':>16s}{'arm last100M':>14s}{'ctrl same win':>15s}{'run1 4.5-5.0B':>15s}")
for k in keys:
    a_all=win(ra,START,ts+1,k); a_l=win(ra,max(START,ts-100e6),ts+1,k)
    c=win(rc,START,START+max(done,1)+1,k) if arm!='ctrl' else None
    o=win(r1,4.5e9,5.0e9,k)
    print(f"{k:36s}{f(a_all):>16s}{f(a_l):>14s}{f(c):>15s}{f(o):>15s}")
