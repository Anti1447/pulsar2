#!/usr/bin/env python3
FAILED=False
"""High-power gauntlet: each fork arm's latest checkpoint vs the four external bots, GGL_H2H_GAMES games per bot (default 512), CPU, niced.
usage: h2h_bots.py ctrl ctrl2 kick ...   Results append to build/gaunt_hp_results.jsonl (arm, bot, wins/losses/draws, first touch); rows carry mean_s (mean game seconds = time until the first goal or the no-touch draw; a continuous, higher-power read than win counts) from 2026-11-21."""
import json, os, subprocess, sys
R='/home/nhafe/pulsar2/build/'; OUT=R+'gaunt_hp_results.jsonl'
env=dict(os.environ, OMP_NUM_THREADS='4', OPENBLAS_NUM_THREADS='4', MKL_NUM_THREADS='4', GGL_DEVICE='cpu', GGL_H2H_BOTS='1', GGL_H2H_GAMES=os.environ.get('GGL_H2H_GAMES','512'))
for arm in sys.argv[1:]:
    p=subprocess.run(['nice','-n','19','./h2h_eval',f'--gco-fork-{arm}'],cwd=R,env=env,capture_output=True,text=True)
    for line in p.stdout.splitlines():
        if line.startswith('GAUNT_RESULT '):
            r=json.loads(line[13:]); r['arm']=arm; open(OUT,'a').write(json.dumps(r)+'\n'); print(json.dumps(r),flush=True)
    if p.returncode!=0: print('exit',p.returncode,p.stderr[-300:],flush=True); FAILED=True
sys.exit(1 if FAILED else 0)
