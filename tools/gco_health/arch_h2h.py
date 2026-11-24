#!/usr/bin/env python3
"""Cross-architecture round robin of the four ts2 architecture-comparison arms' FINAL checkpoints (H-014..H-017). For each pair, the subject (loaded from its arm's folder, newest checkpoint) plays the other
checkpoint, which is rebuilt in ITS OWN architecture via the GGL_H2H_OPP '@<sw>x<sd>/<pw>x<pd>' suffix (Learner.cpp). Both sides argmax, fuzzed kickoffs, 128 arenas, 900 sim s per side.
Share = goalsFor/(goalsFor+goalsAgainst); Elo = 400 log10(s/(1-s)); the SE is inflated 1.5x over binomial (as the earlier h2h tools). Output: build/arch_h2h.jsonl and a table on stdout.
usage: arch_h2h.py [--arms ts2,ts2deep,...]  (default: all four, in build/protected_ts2/<arm>_final_*)"""
import glob, json, math, os, subprocess, sys
B='/home/nhafe/pulsar2/build/'; OUT=B+'arch_h2h.jsonl'
ARCH={'ts2':'1152x2/1024x4','ts2deep':'1152x4/1024x6','ts2critdeep':'1152x2/1024x4','ts2wide':'1728x2/1536x4'}
arms=(sys.argv[sys.argv.index('--arms')+1].split(',') if '--arms' in sys.argv else list(ARCH))
final={}
for a in arms:
    c=sorted(glob.glob(B+f'protected_ts2/{a}_final_*'), key=lambda p:int(p.rsplit('_',1)[1]))
    if c: final[a]=c[-1]
    else: print(f'{a}: no final copy yet, skipped')
env=dict(os.environ, OMP_NUM_THREADS='4', OPENBLAS_NUM_THREADS='4', MKL_NUM_THREADS='4', GGL_DEVICE='cpu', GGL_H2H_GAMES=os.environ.get('GGL_H2H_GAMES','128'))
names=list(final); res={}
for i,a in enumerate(names):
    for b in names[i+1:]:
        env['GGL_H2H_OPP']=f'{final[b]}@{ARCH[b]}'
        p=subprocess.run(['nice','-n','19','./h2h_eval',f'--gco-fork-{a}'],cwd=B,env=env,capture_output=True,text=True)
        line=[l for l in p.stdout.splitlines() if l.startswith('H2H_RESULT ')]
        if p.returncode!=0 or not line: print(f'{a} vs {b}: FAILED rc={p.returncode} {p.stderr[-300:]}'); continue
        r=json.loads(line[0][11:]); r['arm_a']=a; r['arm_b']=b; open(OUT,'a').write(json.dumps(r)+'\n')
        f,g=r['for'],r['against']; n=f+g; s=f/n if n else float('nan')
        elo=400*math.log10(s/(1-s)) if 0<s<1 else float('nan'); se=1.5*400/math.log(10)/math.sqrt(n*s*(1-s)) if 0<s<1 else float('nan')
        res[(a,b)]=(f,g,s,elo,se); print(f'{a:12s} vs {b:12s}  goals {f}-{g}  share {s:.3f}  Elo {elo:+.0f} +-{se:.0f}',flush=True)
sys.exit(0 if res else 1)
