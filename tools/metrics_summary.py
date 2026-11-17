#!/usr/bin/env python3
"""Summarise the trainer's local metrics copy (GigaLearnCPP Learner: <checkpointFolder>/metrics.jsonl).

  tools/metrics_summary.py [--file build/checkpoints_gco_6.1/metrics.jsonl] [--last N] [--grep REGEX] [--latest]

--latest prints every key of metrics_latest.json (the newest iteration). Otherwise prints median / IQR / last of every key
matching --grep over the last N lines (default 60 lines = ~600 iterations).
"""
import argparse, json, os, re, statistics as st
ap = argparse.ArgumentParser()
ap.add_argument('--file', default='build/checkpoints_gco_6.1/metrics.jsonl')
ap.add_argument('--last', type=int, default=60)
ap.add_argument('--grep', default='.')
ap.add_argument('--latest', action='store_true')
a = ap.parse_args()
rx = re.compile(a.grep)
if a.latest:
    p = os.path.join(os.path.dirname(a.file), 'metrics_latest.json')
    j = json.load(open(p))
    print('ts', j['ts'], 'iter', j['iter'])
    for k in sorted(j['m']):
        if rx.search(k): print(f"{k}: {j['m'][k]:.6g}")
    raise SystemExit
rows = [json.loads(l) for l in open(a.file) if l.strip()][-a.last:]
print('lines', len(rows), 'steps', rows[0]['ts'], '->', rows[-1]['ts'])
keys = sorted({k for r in rows for k in r['m'] if rx.search(k)})
for k in keys:
    v = [r['m'][k] for r in rows if k in r['m']]
    q = sorted(v); n = len(q)
    print(f"{k}: n={n} med={q[n//2]:.4g} IQR={q[n//4]:.4g}..{q[3*n//4]:.4g} last={v[-1]:.4g}")
