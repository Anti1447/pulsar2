#!/bin/bash
# usage: chain_arm.sh <arm> <wait-phrase> <done-phrase>. Waits for <wait-phrase> in build/forks_status.log, then runs fork arm <arm> for +500M from 4,995,887,104
# (needs build/checkpoints_gco_fork_<arm>/4995887104 seeded), 100M health reads to build/forks_health.log, logs <done-phrase> at the end.
export PATH=/usr/local/bin:/usr/bin:/bin:$HOME/.local/bin:$PATH
cd /home/nhafe/pulsar2
arm=$1; WAITP=$2; DONEP=$3
ST=build/forks_status.log; HL=build/forks_health.log
say() { echo "$(date '+%F %T') $*" >> $ST; }
alive() { for p in /proc/[0-9]*/exe; do readlink $p 2>/dev/null | grep -q build/GigaLearnBot && return 0; done; return 1; }
lastts() { tail -1 "$1" 2>/dev/null | python3 -c "import sys,json;print(int(json.loads(sys.stdin.read())['ts']))" 2>/dev/null; }
stop_trainer() { tools/trainerctl stop >/dev/null 2>&1; for i in $(seq 1 100); do alive || return 0; sleep 3; done; return 1; }
START=4995887104; END=$(( START + 500000000 ))
say "chain for arm $arm started; waiting for '$WAITP'"
until grep -q "$WAITP" $ST; do sleep 30; done
sleep 20; alive && { say "ABORT $arm: trainer still alive after '$WAITP'"; exit 1; }
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
say "$DONEP"
