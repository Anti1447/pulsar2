#!/usr/bin/env python3
"""Research ledger (docs/ledger/H-*.md). One file per hypothesis: a header block of `key: value` lines between two `---` lines, then free text.
  ledger.py list                      table of every entry
  ledger.py show H-004                the entry
  ledger.py status H-004 running "note"   set status (proposed|approved|queued|running|done|evaluated|replicate|dropped) and append a dated line to the Log section
  ledger.py next                      the first APPROVED entry whose `depends` are all done/evaluated (what the launcher should run next)
Header keys: id, title, status, arm, depends (comma list of ids), gpu_h, prior (low|medium|high), expect (JSON subset the launcher checks against the arm's CONFIG_DUMP)."""
import glob, json, os, re, sys, datetime
D=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','docs','ledger')
STATUSES=['proposed','approved','queued','running','done','evaluated','replicate','dropped']
def load(path):
    t=open(path).read(); m=re.match(r'---\n(.*?)\n---\n(.*)',t,re.S)
    h={}
    for l in m.group(1).splitlines():
        if ':' in l: k,v=l.split(':',1); h[k.strip()]=v.strip()
    h['_path']=path; h['_body']=m.group(2); return h
def entries(): return [load(p) for p in sorted(glob.glob(os.path.join(D,'H-*.md')))]
def get(i):
    for e in entries():
        if e['id']==i: return e
    sys.exit(f'no entry {i}')
def setstatus(i,new,note=''):
    assert new in STATUSES, f'status must be one of {STATUSES}'
    e=get(i); t=open(e['_path']).read()
    t=re.sub(r'(?m)^status:.*$',f'status: {new}',t,count=1)
    if '## Log' not in t: t+='\n## Log\n'
    t=t.rstrip('\n')+f"\n- {datetime.datetime.now():%Y-%m-%d %H:%M} -> {new}" + (f": {note}" if note else '') + '\n'
    open(e['_path'],'w').write(t)
def nxt():
    es=entries(); st={e['id']:e['status'] for e in es}
    for e in es:
        if e['status']!='approved': continue
        deps=[d.strip() for d in e.get('depends','').split(',') if d.strip()]
        if all(st.get(d) in ('done','evaluated') for d in deps): return e
if __name__=='__main__':
    a=sys.argv[1:]
    if not a or a[0]=='list':
        print(f"{'id':6s}{'status':11s}{'arm':10s}{'gpu_h':>6s}  {'prior':7s}title")
        for e in entries(): print(f"{e['id']:6s}{e['status']:11s}{e.get('arm','-'):10s}{e.get('gpu_h','-'):>6s}  {e.get('prior','-'):7s}{e['title']}")
    elif a[0]=='show': e=get(a[1]); print(open(e['_path']).read())
    elif a[0]=='status': setstatus(a[1],a[2],a[3] if len(a)>3 else '')
    elif a[0]=='next':
        e=nxt(); print(e['id']+' '+e['title'] if e else 'nothing approved and unblocked')
