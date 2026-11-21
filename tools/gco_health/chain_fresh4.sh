#!/bin/bash
# Waits for "CTRL2 ALL DONE" in build/forks_status.log, then launches the critic-heads fresh run (--gco-fresh4) and stops it at 5.0B steps.
# 100M health reads -> build/fresh4_health.log, status -> build/fresh4_status.log. Launch: systemd-run --user --unit=gco-fresh4-chain tools/gco_health/chain_fresh4.sh
export PATH=/usr/local/bin:/usr/bin:/bin:$HOME/.local/bin:$PATH
cd /home/nhafe/pulsar2
WAIT_ST=build/forks_status.log; ST=build/fresh4_status.log; HL=build/fresh4_health.log
say() { echo "$(date '+%F %T') $*" >> $ST; }
alive() { for p in /proc/[0-9]*/exe; do readlink $p 2>/dev/null | grep -q build/GigaLearnBot && return 0; done; return 1; }
lastts() { tail -1 "$1" 2>/dev/null | python3 -c "import sys,json;print(int(json.loads(sys.stdin.read())['ts']))" 2>/dev/null; }
stop_trainer() { tools/trainerctl stop >/dev/null 2>&1; for i in $(seq 1 100); do alive || return 0; sleep 3; done; return 1; }
END=5000000000
say "fresh4 chain started; waiting for CTRL2 ALL DONE"
until grep -qE " CTRL2 ALL DONE$" $WAIT_ST; do sleep 30; done
sleep 20; alive && { say "ABORT: trainer still alive after CTRL2 ALL DONE"; exit 1; }
[ -e build/checkpoints_gco_fresh4 ] && { say "ABORT: build/checkpoints_gco_fresh4 already exists (refusing to resume or overwrite a fresh start)"; exit 1; }
say "starting fresh4"
( cd build && ../tools/run_trainer.sh --detach -- ./GigaLearnBot --gco-fresh4 >/dev/null 2>&1 )
sleep 5
L=$(ls -t run_logs/train-*.log | head -1)
for i in $(seq 1 120); do grep -q -E "Total Iterations|ERROR|Exception|terminate" $L && break; sleep 3; done
if ! grep -q "Critic heads ENABLED" $L || ! grep -q 'Checkpoint Save/Load Dir: "checkpoints_gco_fresh4"' $L; then
  say "ABORT: fresh4 did not boot with the heads on and its own folder (log $L)"; grep -E "Checkpoint Save/Load Dir|Critic heads|ERROR|Exception" $L | cut -c1-160 >> $ST; exit 1
fi
say "fresh4 booted: log $L"
next=100000000
while true; do
  ts=$(lastts build/checkpoints_gco_fresh4/metrics.jsonl)
  if [ -n "$ts" ] && [ "$ts" -ge "$next" ]; then
    { echo "=== fresh4 health at ${ts} ==="; OMP_NUM_THREADS=2 nice -n 19 python3 tools/gco_health/fresh4_read.py; } >> $HL 2>&1
    next=$(( next + 100000000 ))
  fi
  [ -n "$ts" ] && [ "$ts" -ge "$END" ] && break
  alive || { say "ABORT: trainer died at ts=$ts"; exit 1; }
  sleep 30
done
stop_trainer || { say "ABORT: fresh4 would not stop"; exit 1; }
say "DONE fresh4 at ts=$ts, final checkpoint $(ls -t build/checkpoints_gco_fresh4 | grep -E '^[0-9]+$' | head -1)"
say "FRESH4 ALL DONE"
