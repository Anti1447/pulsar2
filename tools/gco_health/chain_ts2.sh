#!/bin/bash
# TICKSKIP-2 RUN (user-directed 2026-11-22, docs/GCO_TS2.md): fresh network, tickSkip 2 / actionDelay 1, gammas x4, Nexto+Chronos 0.30, near-goal reset mix.
# At 1B steps and every 250M after, check the wall (ts2_read.py --check). When broken: touch KICK_MIX in the run folder (selects the kickoff-dense mix, near-goal reduced),
# stop and restart the trainer once (resumes the same folder), then read health every 250M indefinitely. Never stops the run otherwise.
export PATH=/usr/local/bin:/usr/bin:/bin:$HOME/.local/bin:$PATH
cd /home/nhafe/pulsar2
ST=build/ts2_status.log; HL=build/ts2_health.log; F=build/checkpoints_gco_fork_ts2; P=build/protected_ts2; mkdir -p $P
say() { echo "$(date '+%F %T') $*" >> $ST; }
alive() { for p in /proc/[0-9]*/exe; do readlink $p 2>/dev/null | grep -q build/GigaLearnBot && return 0; done; return 1; }
lastts() { tail -1 "$1" 2>/dev/null | python3 -c "import sys,json;print(int(json.loads(sys.stdin.read())['ts']))" 2>/dev/null; }
stop_trainer() { tools/trainerctl stop >/dev/null 2>&1; for i in $(seq 1 100); do alive || return 0; sleep 3; done; return 1; }
start_trainer() { ( cd build && ../tools/run_trainer.sh --detach -- ./GigaLearnBot --gco-fork-ts2 >/dev/null 2>&1 ); }
if ! alive; then say "launching ts2 (fresh)"; start_trainer; sleep 20; else say "trainer already running; attaching"; fi
next=250000000; evalnext=1000000000; switched=0; [ -f $F/KICK_MIX ] && switched=1
while true; do
  ts=$(lastts $F/metrics.jsonl)
  if [ -n "$ts" ] && [ "$ts" -ge "$next" ]; then
    { echo "=== ts2 health at ${ts} ==="; OMP_NUM_THREADS=2 nice -n 19 python3 tools/gco_health/ts2_read.py; } >> $HL 2>&1; next=$(( next + 250000000 ))
  fi
  if [ -n "$ts" ] && [ "$switched" = 0 ] && [ "$ts" -ge "$evalnext" ]; then
    r=$(OMP_NUM_THREADS=2 nice -n 19 python3 tools/gco_health/ts2_read.py --check 2>&1); rc=$?
    say "wall check at ts=$ts: $r"
    evalnext=$(( evalnext + 250000000 ))
    if [ $rc = 0 ]; then
      c=$(ls -d $F/[0-9]* | sort -V | tail -1); cp -r $c $P/before_kickmix_$(basename $c); say "protected copy $P/before_kickmix_$(basename $c)"
      touch $F/KICK_MIX; say "KICK_MIX marker written; restarting the trainer on the kickoff-dense mix"
      stop_trainer || { say "ABORT: trainer would not stop"; exit 1; }
      start_trainer; switched=1; sleep 60
      alive && say "restarted on the kick mix" || say "ERROR: trainer did not come back after the mix switch"
    fi
  fi
  sleep 60
done
