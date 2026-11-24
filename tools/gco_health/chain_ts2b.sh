#!/bin/bash
# ts2 run, user-directed 2026-11-23: STOP at 1B steps (replaces chain_ts2.sh: no mix switch, no further reads). Logs the wall check at the stop, copies the final checkpoint to build/protected_ts2.
export PATH=/usr/local/bin:/usr/bin:/bin:$HOME/.local/bin:$PATH
cd /home/nhafe/pulsar2
ST=build/ts2_status.log; HL=build/ts2_health.log; F=build/checkpoints_gco_fork_ts2; P=build/protected_ts2; mkdir -p $P
say() { echo "$(date '+%F %T') $*" >> $ST; }
alive() { for p in /proc/[0-9]*/exe; do readlink $p 2>/dev/null | grep -q build/GigaLearnBot && return 0; done; return 1; }
lastts() { tail -1 "$1" 2>/dev/null | python3 -c "import sys,json;print(int(json.loads(sys.stdin.read())['ts']))" 2>/dev/null; }
say "chain_ts2b attached: will stop the trainer at 1,000,000,000 steps"
next=250000000
ts=$(lastts $F/metrics.jsonl); while [ -n "$ts" ] && [ "$next" -le "$ts" ]; do next=$(( next + 250000000 )); done
while true; do
  ts=$(lastts $F/metrics.jsonl)
  if [ -n "$ts" ] && [ "$ts" -ge "$next" ] && [ "$next" -lt 1000000000 ]; then
    { echo "=== ts2 health at ${ts} ==="; OMP_NUM_THREADS=2 nice -n 19 python3 tools/gco_health/ts2_read.py; } >> $HL 2>&1; next=$(( next + 250000000 ))
  fi
  if [ -n "$ts" ] && [ "$ts" -ge 1000000000 ]; then break; fi
  alive || { say "ABORT: trainer died at ts=$ts"; exit 1; }
  sleep 30
done
tools/trainerctl stop >/dev/null 2>&1; for i in $(seq 1 100); do alive || break; sleep 3; done
alive && { say "ERROR: trainer would not stop"; exit 1; }
{ echo "=== ts2 FINAL health at ${ts} ==="; OMP_NUM_THREADS=2 nice -n 19 python3 tools/gco_health/ts2_read.py; } >> $HL 2>&1
r=$(OMP_NUM_THREADS=2 nice -n 19 python3 tools/gco_health/ts2_read.py --check 2>&1); say "wall check at stop: $r"
c=$(ls -d $F/[0-9]* | sort -V | tail -1); cp -r $c $P/final_$(basename $c); say "TS2 STOPPED at ts=$ts; final copy $P/final_$(basename $c)"
