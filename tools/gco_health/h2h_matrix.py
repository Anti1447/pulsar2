#!/usr/bin/env python3
"""Offline head-to-head matrix of the fork arms (docs/GCO_H2H_EVAL.md). usage: h2h_matrix.py A:B,C,... [A2:B2,...]  (arms: ctrl kick opp kickopp ctrl2; B may also be 'start').
Results append to build/h2h_results.jsonl; pairs already present are skipped."""
import json, os, re, subprocess, sys, math
R='/home/nhafe/pulsar2/build/'; OUT=R+'h2h_results.jsonl'
STARTDIR=R+'checkpoints_gco_fresh_FINAL_4995887104_BACKUP'
def latest(arm):
    d=R+f'checkpoints_gco_fork_{arm}'
    c=sorted(int(x) for x in os.listdir(d) if re.fullmatch(r'\d+',x))
    return f'{d}/{c[-1]}'
def done():
    s=set()
    try:
        for l in open(OUT):
            r=json.loads(l); s.add((r['A'],r['B'],r.get('det',1)))
    except FileNotFoundError: pass
    return s
env=dict(os.environ, OMP_NUM_THREADS='4', OPENBLAS_NUM_THREADS='4', MKL_NUM_THREADS='4', GGL_H2H_GAMES=os.environ.get('GGL_H2H_GAMES','128'), GGL_H2H_SECS=os.environ.get('GGL_H2H_SECS','900'))
for spec in sys.argv[1:]:
    A,Bs=spec.split(':'); Bs=Bs.split(',')
    todo=[b for b in Bs if (A,b,0 if env.get('GGL_H2H_STOCH')=='1' else 1) not in done()]
    if not todo: continue
    dirs=[STARTDIR if b=='start' else latest(b) for b in todo]
    e=dict(env, GGL_H2H_OPP=','.join(dirs))
    p=subprocess.run(['nice','-n','19','./h2h_eval',f'--gco-fork-{A}'],cwd=R,env=e,capture_output=True,text=True)
    for line in p.stdout.splitlines():
        if line.startswith('H2H_RESULT '):
            r=json.loads(line[11:]); b=[x for x,d in zip(todo,dirs) if d==r['b']][0]
            n=r['for']+r['against']; s=r['for']/n if n else float('nan'); se=1.5*math.sqrt(s*(1-s)/n) if n else float('nan')
            rec=dict(A=A,B=b,a_ts=r['a_ts'],b_dir=r['b'],goals_for=r['for'],goals_against=r['against'],share=s,se_eff=se,elo=400*math.log10(s/(1-s)) if 0<s<1 else None,det=r['det'],arenas=r['arenas'],secs=r['secs'])
            open(OUT,'a').write(json.dumps(rec)+'\n'); print(json.dumps(rec),flush=True)
    if p.returncode!=0: print('h2h_eval exit',p.returncode,p.stderr[-300:],flush=True)
