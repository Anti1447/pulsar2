#!/bin/bash
# H-015 (user-directed 2026-11-23): fresh ts2 run with every network DEEPER (residual blocks added; revised from a width plan) (--gco-fork-ts2deep). Waits for the ts2 run to stop at 1B ("TS2 STOPPED" in build/ts2_status.log),
# launches, reads health every 250M against ts2 at the same step count (TS2_ARM=ts2deep), and STOPS at 1B for a paired comparison. Logs to build/ts2deep_status.log / ts2deep_health.log.
export PATH=/usr/local/bin:/usr/bin:/bin:$HOME/.local/bin:$PATH
cd /home/nhafe/pulsar2
ST=build/ts2deep_status.log; HL=build/ts2deep_health.log; F=build/checkpoints_gco_fork_ts2deep; P=build/protected_ts2; mkdir -p $P
say() { echo "$(date '+%F %T') $*" >> $ST; }
alive() { for p in /proc/[0-9]*/exe; do readlink $p 2>/dev/null | grep -q build/GigaLearnBot && return 0; done; return 1; }
lastts() { tail -1 "$1" 2>/dev/null | python3 -c "import sys,json;print(int(json.loads(sys.stdin.read())['ts']))" 2>/dev/null; }
say "waiting for TS2 STOPPED"
until grep -q "TS2 STOPPED" build/ts2_status.log 2>/dev/null; do sleep 60; done
for i in $(seq 1 60); do alive || break; sleep 5; done
alive && { say "ABORT: a trainer is still running after ts2 stopped"; exit 1; }
say "launching ts2deep (fresh)"; ( cd build && ../tools/run_trainer.sh --detach -- ./GigaLearnBot --gco-fork-ts2deep >/dev/null 2>&1 ); sleep 30
alive || { say "ABORT: ts2deep did not start"; exit 1; }
next=250000000
while true; do
  ts=$(lastts $F/metrics.jsonl)
  if [ -n "$ts" ] && [ "$ts" -ge "$next" ] && [ "$next" -lt 1000000000 ]; then
    { echo "=== ts2deep health at ${ts} ==="; TS2_ARM=ts2deep OMP_NUM_THREADS=2 nice -n 19 python3 tools/gco_health/ts2_read.py; } >> $HL 2>&1; next=$(( next + 250000000 ))
  fi
  [ -n "$ts" ] && [ "$ts" -ge 1000000000 ] && break
  alive || { say "ABORT: trainer died at ts=$ts"; exit 1; }
  sleep 30
done
tools/trainerctl stop >/dev/null 2>&1; for i in $(seq 1 100); do alive || break; sleep 3; done
{ echo "=== ts2deep FINAL health at ${ts} ==="; TS2_ARM=ts2deep OMP_NUM_THREADS=2 nice -n 19 python3 tools/gco_health/ts2_read.py; } >> $HL 2>&1
c=$(ls -d $F/[0-9]* | sort -V | tail -1); cp -r $c $P/deep_final_$(basename $c); say "TS2BIG STOPPED at ts=$ts; final copy $P/deep_final_$(basename $c)"
