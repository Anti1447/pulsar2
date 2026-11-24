#!/bin/bash
# ARCHITECTURE COMPARISON (user-directed 2026-11-23, ledger H-014..H-017): four ts2 runs to 2B steps each, run one after another on the single GPU:
#   ts2 (control, already running) -> ts2deep (all nets deeper) -> ts2critdeep (critic stack deeper only) -> ts2wide (all nets 1.5x wider).
# Everything but the network sizes is identical (seed, mix rule, opponents, horizons, LR). The SAME mix rule applies to each: at 1B steps and every 250M after, if the wall is broken
# (ts2_read.py --check) write KICK_MIX and restart once on the kickoff-dense mix. The step of that switch is logged per arm (it can differ by arm: read the pre-switch window as the clean comparison).
# Health reads every 250M (TS2_ARM=<arm>; ref = ts2 at the same step, ts2 itself vs fresh3). Stops each arm at 2B, copies the final checkpoint to build/protected_ts2, then runs the
# 1024-game external-bot gauntlet on it in the background (niced CPU). Resumable: an arm with "ARCH <arm> DONE" in the status log is skipped.
export PATH=/usr/local/bin:/usr/bin:/bin:$HOME/.local/bin:$PATH
cd /home/nhafe/pulsar2
ST=build/arch_status.log; HL=build/arch_health.log; P=build/protected_ts2; mkdir -p $P
ARMS="ts2 ts2deep ts2critdeep ts2wide"; END=2000000000
say() { echo "$(date '+%F %T') $*" >> $ST; }
alive() { for p in /proc/[0-9]*/exe; do readlink $p 2>/dev/null | grep -q build/GigaLearnBot && return 0; done; return 1; }
lastts() { tail -1 "$1" 2>/dev/null | python3 -c "import sys,json;print(int(json.loads(sys.stdin.read())['ts']))" 2>/dev/null; }
stop_trainer() { tools/trainerctl stop >/dev/null 2>&1; for i in $(seq 1 100); do alive || return 0; sleep 3; done; return 1; }
start_trainer() { ( cd build && ../tools/run_trainer.sh --detach -- ./GigaLearnBot --gco-fork-$1 >/dev/null 2>&1 ); }
say "chain_arch attached; arms: $ARMS; each to $END steps"
for ARM in $ARMS; do
  grep -q "ARCH $ARM DONE" $ST 2>/dev/null && continue
  F=build/checkpoints_gco_fork_$ARM
  if ! alive; then say "launching $ARM"; start_trainer $ARM; sleep 30; alive || { say "ABORT: $ARM did not start"; exit 1; }; else say "trainer running; attaching to $ARM"; fi
  next=250000000; evalnext=1000000000; switched=0; [ -f $F/KICK_MIX ] && switched=1
  ts=$(lastts $F/metrics.jsonl); while [ -n "$ts" ] && [ "$next" -le "$ts" ]; do next=$(( next + 250000000 )); done
  while true; do
    ts=$(lastts $F/metrics.jsonl)
    if [ -n "$ts" ] && [ "$ts" -ge "$next" ]; then
      { echo "=== $ARM health at ${ts} ==="; TS2_ARM=$ARM OMP_NUM_THREADS=2 nice -n 19 python3 tools/gco_health/ts2_read.py; } >> $HL 2>&1; next=$(( next + 250000000 ))
    fi
    if [ -n "$ts" ] && [ "$switched" = 0 ] && [ "$ts" -ge "$evalnext" ] && [ "$ts" -lt "$END" ]; then
      r=$(TS2_ARM=$ARM OMP_NUM_THREADS=2 nice -n 19 python3 tools/gco_health/ts2_read.py --check 2>&1); rc=$?
      say "$ARM wall check at ts=$ts: $r"; evalnext=$(( evalnext + 250000000 ))
      if [ $rc = 0 ]; then
        c=$(ls -d $F/[0-9]* | sort -V | tail -1); cp -r $c $P/${ARM}_before_kickmix_$(basename $c)
        touch $F/KICK_MIX; say "$ARM KICK_MIX switch at ts=$ts (protected copy ${ARM}_before_kickmix_$(basename $c)); restarting on the kick mix"
        stop_trainer || { say "ABORT: trainer would not stop"; exit 1; }
        start_trainer $ARM; switched=1; sleep 60; alive || { say "ERROR: $ARM did not come back after the switch"; exit 1; }
      fi
    fi
    [ -n "$ts" ] && [ "$ts" -ge "$END" ] && break
    alive || { sleep 120; alive || { say "ABORT: $ARM trainer died at ts=$ts (the wrapper did not restart it)"; exit 1; }; }
    sleep 30
  done
  stop_trainer || { say "ABORT: would not stop"; exit 1; }
  { echo "=== $ARM FINAL health at ${ts} ==="; TS2_ARM=$ARM OMP_NUM_THREADS=2 nice -n 19 python3 tools/gco_health/ts2_read.py; } >> $HL 2>&1
  c=$(ls -d $F/[0-9]* | sort -V | tail -1); cp -r $c $P/${ARM}_final_$(basename $c)
  say "ARCH $ARM DONE at ts=$ts; final copy $P/${ARM}_final_$(basename $c); kick-mix switched: $([ -f $F/KICK_MIX ] && echo yes || echo no)"
  ( cd build; cp GigaLearnBot h2h_eval.new && mv -f h2h_eval.new h2h_eval; GGL_H2H_GAMES=1024 OMP_NUM_THREADS=4 nice -n 19 python3 ../tools/gco_health/h2h_bots.py $ARM >> arch_status.log 2>&1; echo "$(date '+%F %T') gauntlet-1024 $ARM finished" >> arch_status.log ) &
done
wait
say "ARCH ALL DONE"
