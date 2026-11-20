#!/bin/bash
# Waits for fresh3 to pass 1B steps, stops it, then runs the three post-wall fork arms (ctrl, kick, opp) for +500M steps each, in order.
# Status: build/forks_status.log. 100M health reads: build/forks_health.log. Launch: systemd-run --user --unit=gco-forks-chain tools/gco_health/chain_forks.sh
export PATH=/usr/local/bin:/usr/bin:/bin:$HOME/.local/bin:$PATH
cd /home/nhafe/pulsar2
ST=build/forks_status.log; HL=build/forks_health.log
say() { echo "$(date '+%F %T') $*" >> $ST; }
alive() { for p in /proc/[0-9]*/exe; do readlink $p 2>/dev/null | grep -q build/GigaLearnBot && return 0; done; return 1; }
lastts() { tail -1 "$1" 2>/dev/null | python3 -c "import sys,json;print(int(json.loads(sys.stdin.read())['ts']))" 2>/dev/null; }
stop_trainer() { tools/trainerctl stop >/dev/null 2>&1; for i in $(seq 1 100); do alive || return 0; sleep 3; done; return 1; }
START=4995887104; END=$(( START + 500000000 ))
say "chain (re)started; waiting for fresh3 >= 1B"
while true; do
  ts=$(lastts build/checkpoints_gco_fresh3/metrics.jsonl)
  [ -n "$ts" ] && [ "$ts" -ge 1000000000 ] && break
  alive || { say "ABORT: no trainer running while waiting for fresh3 (ts=$ts); nothing started"; exit 1; }
  sleep 30
done
say "fresh3 reached $ts; stopping it"
stop_trainer || { say "ABORT: fresh3 would not stop"; exit 1; }
say "fresh3 final checkpoint: $(ls -t build/checkpoints_gco_fresh3 | grep -E '^[0-9]+$' | head -1)"
for arm in ctrl kick opp; do
  say "starting arm $arm"
  ( cd build && ../tools/run_trainer.sh --detach -- ./GigaLearnBot --gco-fork-$arm >/dev/null 2>&1 )
  sleep 5
  L=$(ls -t run_logs/train-*.log | head -1)
  for i in $(seq 1 120); do grep -q -E "Total Iterations|ERROR|Exception|terminate" $L && break; sleep 3; done
  if ! grep -q "Loading checkpoint \"checkpoints_gco_fork_$arm/$START\"" $L; then
    say "ABORT: arm $arm did not load checkpoints_gco_fork_$arm/$START (log $L)"; grep -E "Checkpoint Save/Load Dir|Loading checkpoint|ERROR|Exception" $L | cut -c1-160 >> $ST; exit 1
  fi
  say "arm $arm booted: $(grep -E 'Total Iterations' $L | head -1)  log $L"
  nexthealth=$(( START + 100000000 ))
  while true; do
    ts=$(lastts build/checkpoints_gco_fork_$arm/metrics.jsonl)
    if [ -n "$ts" ] && [ "$ts" -ge "$nexthealth" ]; then
      { echo "=== $arm health at ${ts} ==="; OMP_NUM_THREADS=2 nice -n 19 python3 tools/gco_health/fork_read.py $arm; } >> $HL 2>&1
      nexthealth=$(( nexthealth + 100000000 ))
    fi
    [ -n "$ts" ] && [ "$ts" -ge "$END" ] && break
    alive || { say "ABORT: trainer died during arm $arm at ts=$ts"; exit 1; }
    sleep 30
  done
  stop_trainer || { say "ABORT: arm $arm would not stop"; exit 1; }
  say "DONE $arm at ts=$ts, final checkpoint $(ls -t build/checkpoints_gco_fork_$arm | grep -E '^[0-9]+$' | head -1)"
done
say "ALL DONE"
