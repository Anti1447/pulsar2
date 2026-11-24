#!/usr/bin/env python3
"""Health read + wall-break check for the tickSkip-2 run (docs/GCO_TS2.md). usage: ts2_read.py [--check]
Prints the last-100M window of the key panels next to fresh3 (the 15 Hz near-goal-mix run) at the SAME NUMBER OF SIM-SECONDS (ts2 steps / 4), because one ts2 step is 1/4 the game time.
--check exits 0 if the wall is broken (rule below), 1 if not, 2 if no data.
WALL BROKEN (pre-registered 2026-11-22, converted from the 15 Hz runs' rule 'no-touch penalty ~0 and episode length > 600 steps = 40 s'): over the last 100M steps
  (a) 4 x mean(no-touch-timeout penalty per step) > -0.0005   (per-step rate is 1/4 of its 15 Hz value at equal behaviour; fresh3: -0.0027 before the wall, -0.0004 at 600-700M, -0.00002 after)
  (b) [dropped 2026-11-23, see the code comment] mean episode length in SECONDS = steps * 2 / 120 > 40"""
import json, os, sys, statistics as st
ARM=os.environ.get('TS2_ARM','ts2')  # ts2 | ts2big (the 1.5x wider run, compared with ts2 at the SAME step count)
B='/home/nhafe/pulsar2/build/'; F=B+f'checkpoints_gco_fork_{ARM}/metrics.jsonl'
def load(f):
    try: return [json.loads(l) for l in open(f)]
    except Exception: return []
ra=load(F); rf=load(B+('checkpoints_gco_fresh3/metrics.jsonl' if ARM=='ts2' else 'checkpoints_gco_fork_ts2/metrics.jsonl'))
SCALE=4 if ARM=='ts2' else 1  # fresh3 is a 15 Hz run (4 ts2 steps per fresh3 step); ts2 is the same step rate
if not ra: print('ts2: no metrics yet'); sys.exit(2)
ts=ra[-1]['ts']
PEN='Rewards/N4RLGC21NoTouchTimeoutPenaltyE'
def win(rows,lo,hi,k):
    v=[r['m'][k] for r in rows if k in r['m'] and lo<=r['ts']<hi]; return st.mean(v) if v else None
f=lambda x:'-' if x is None else f"{x:.4g}"
lo=max(0,ts-100e6)
pen=win(ra,lo,ts+1,PEN); ep=win(ra,lo,ts+1,'Episode Length')
epsec=None if ep is None else ep*2/120; pen4=None if pen is None else 4*pen
broken = pen4 is not None and pen4>-0.0005  # rule (b), episode > 40 s, DROPPED 2026-11-23: with Nexto/Chronos served 30%, post-wall episodes average ~28 s (nexchron2: 28.1 s at penalty -0.00002), so it could never fire
if '--check' in sys.argv:
    print(f"ts {ts/1e6:.0f}M window {lo/1e6:.0f}-{ts/1e6:.0f}M: 4*penalty {f(pen4)} (need > -0.0005), episode {f(epsec)} s (info only) -> {'BROKEN' if broken else 'not broken'}")
    sys.exit(0 if broken else 1)
print(f"{ARM} health at {ts/1e6:.0f}M steps ({ts*2/120/3600/1000:.1f}k sim-hours/arena-sum scale: 1 step = 1/60 s), iter {ra[-1]['iter']}")
print(f"wall check: 4*penalty {f(pen4)} (need > -0.0005), episode {f(epsec)} s (info only) -> {'BROKEN' if broken else 'not broken'}")
print(f"{'panel':38s}{'ts2 last100M':>14s}{'ref same sim-time':>22s}")
keys=['Gauntlet/Mean Win %','Gauntlet/Mean First Touch Us %','Gauntlet/Nexto/Win %','Gauntlet/ChronosV2/Win %','Gauntlet/GalactusV2/Win %','Gauntlet/Dice/Win %',
 'GCO/Goals/10k Rows 1v1','Player/Ball Touch Ratio','Player/Aerial Touch Ratio','Episode Length','Policy Entropy','GCO/Critic Explained Variance','Policy Update Magnitude','Overall Steps/Second',PEN]
for k in keys:
    a=win(ra,lo,ts+1,k)
    # fresh3 at the same simulated time: ts2 step s corresponds to fresh3 step s/4
    c=win(rf,lo/SCALE,ts/SCALE+1,k)
    print(f"{k:38s}{f(a):>14s}{f(c):>22s}")
print("(per-step panels scale with the step rate: touch ratio and goals/10k rows read about 1/4 of their 15 Hz values at equal behaviour; compare per second, not per row)")
