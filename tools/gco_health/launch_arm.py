#!/usr/bin/env python3
"""Launch an approved ledger entry's fork arm with its checks and its evals attached.
usage: launch_arm.py H-004 [--after "PHRASE"] [--dry]
Checks (any failure aborts BEFORE anything is queued): the entry is `approved` and its depends are done; the arm name parses in the CURRENT binary (GGL_CONFIG_DUMP, no GPU) and the
effective config matches the entry's `expect` JSON; the arm folder holds only the seeded start checkpoint (created from the backup when absent); no other launch uses the folder.
Then: chain_arm.sh (waits for --after, default: start now, refused if a trainer is alive) -> "<ARM> ALL DONE", eval_arm.sh (head-to-head + 1024-game gauntlet) -> "<ARM> EVALS DONE".
The entry goes `approved -> queued`; the chain sets nothing else (the ledger is updated by whoever reads the result)."""
import json, os, re, shutil, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))+'/..')
import ledger
R='/home/nhafe/pulsar2/'; B=R+'build/'; START='4995887104'; BACKUP=B+'checkpoints_gco_fresh_FINAL_4995887104_BACKUP'
def die(m):
    if dry and (m.startswith('depends') or ' is ' in m and 'not approved' in m): print('WARN (dry):',m); return
    print('ABORT:',m); sys.exit(1)
a=sys.argv[1:]; dry='--dry' in a; after=a[a.index('--after')+1] if '--after' in a else None
if not a or a[0].startswith('--'): sys.exit(__doc__)
e=ledger.get(a[0]); arm=e.get('arm')
if e['status']!='approved': die(f"{e['id']} is {e['status']}, not approved")
st={x['id']:x['status'] for x in ledger.entries()}
for d in [x.strip() for x in e.get('depends','').split(',') if x.strip()]:
    if st.get(d) not in ('done','evaluated'): die(f'depends on {d} ({st.get(d)})')
# 1. the arm parses in the current binary and wires what the entry says
def refresh(dst):  # copy then rename: works while the old copy is still running (no 'Text file busy')
    shutil.copy2(B+'GigaLearnBot', B+dst+'.new'); os.replace(B+dst+'.new', B+dst)
refresh('arm_check')
if not dry: refresh('h2h_eval')
p=subprocess.run(['./arm_check',f'--gco-fork-{arm}'],cwd=B,env=dict(os.environ,GGL_CONFIG_DUMP='1',GGL_DEVICE='cpu'),capture_output=True,text=True,timeout=120)
line=[l for l in p.stdout.splitlines() if l.startswith('CONFIG_DUMP ')]
if not line: die(f'arm {arm} does not parse in the current binary: {(p.stdout+p.stderr)[-200:]}')
cfg=json.loads(line[0][12:]); exp=json.loads(e.get('expect','{}'))
served=sorted(b['name'] for b in cfg['bots'] if b['serve']) if cfg['serve_frac']>0 else []
got={'serve_frac':cfg['serve_frac'],'serve':served,'old_versions':cfg['old_versions'],'kick_mix':cfg['kick_mix'],'folder':cfg['folder']}
for k,v in exp.items():
    if got.get(k)!=(sorted(v) if k=='serve' else v): die(f'config mismatch on {k}: entry expects {v}, the binary has {got.get(k)}')
print('config check OK:',got)
# 2. the folder
d=B+cfg['folder']
if os.path.isdir(d):
    nums=[x for x in os.listdir(d) if re.fullmatch(r'\d+',x)]
    if nums!=[START]: die(f'{d} already holds {nums} (expected only {START})')
elif not dry: os.makedirs(d); shutil.copytree(BACKUP,d+'/'+START); print('seeded',d)
# 3. a trainer must not be running unless we wait behind another arm
def trainer_alive():
    for n in os.listdir('/proc'):
        if n.isdigit():
            try:
                if 'build/GigaLearnBot' in os.readlink(f'/proc/{n}/exe'): return True
            except OSError: pass
    return False
alive=trainer_alive()
if alive and not after: die('a trainer is running; pass --after "<PHRASE>" to queue behind the current arm')
if dry: print('dry run: all checks passed, nothing launched'); sys.exit(0)
U=arm.upper(); wait=after
if not wait:
    wait=f'LAUNCH {e["id"]}'; open(B+'forks_status.log','a').write(time.strftime('%F %T')+f' {wait}\n')
sd=['systemd-run','--user','--collect']
subprocess.run(sd+[f'--unit=ledger-{arm}-chain',R+'tools/gco_health/chain_arm.sh',arm,wait,f'{U} ALL DONE'],check=True)
subprocess.run(sd+[f'--unit=ledger-{arm}-eval',R+'tools/gco_health/eval_arm.sh',arm],check=True)
ledger.setstatus(e['id'],'queued',f'chain+eval armed, waiting on "{wait}"')
print('queued',e['id'],arm)
