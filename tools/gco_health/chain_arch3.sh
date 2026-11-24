#!/bin/bash
# ARCHITECTURE COMPARISON v3 (user-directed 2026-11-23, supersedes chain_arch2.sh, which switched the mix at 1B = only ~250M fresh3-equivalent, pre-wall): four ts2 runs to 5B steps each, one after another.
# ts2 resumes from the PRE-switch copy (1.0018B, near-goal mix); the others run from their (near-fresh) folders. Static mix: near-goal to 4B steps, then KICK_MIX marker, restart; no wall evaluation.
# Order ts2 -> ts2deep -> ts2critdeep -> ts2wide. Health every 250M; at 5B the final copy goes to build/protected_ts2 and a 1024-game gauntlet runs niced; after all arms the cross-arch H2H matrix.
export PATH=/usr/local/bin:/usr/bin:/bin:$HOME/.local/bin:$PATH
ST=build/arch3_status.log; HL=build/arch3_health.log; P=build/protected_ts2; mkdir -p $P
ARMS="ts2 ts2deep ts2critdeep ts2wide"; END=5000000000; SWITCH=4000000000
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
  next=250000000; switched=0; [ -f $F/KICK_MIX ] && switched=1
  ts=$(lastts $F/metrics.jsonl); while [ -n "$ts" ] && [ "$next" -le "$ts" ]; do next=$(( next + 250000000 )); done
  while true; do
    ts=$(lastts $F/metrics.jsonl)
    if [ -n "$ts" ] && [ "$ts" -ge "$next" ]; then
      { echo "=== $ARM health at ${ts} ==="; TS2_ARM=$ARM OMP_NUM_THREADS=2 nice -n 19 python3 tools/gco_health/ts2_read.py; } >> $HL 2>&1; next=$(( next + 250000000 ))
    fi
    if [ -n "$ts" ] && [ "$switched" = 0 ] && [ "$ts" -ge "$SWITCH" ]; then
      # STATIC mix shift (user-directed 2026-11-23): at SWITCH steps, every arm, no wall evaluation. trainerctl stop saves after the current iteration, so the restart resumes at the stop point.
      c=$(ls -d $F/[0-9]* | sort -V | tail -1); cp -r $c $P/${ARM}_before_kickmix_$(basename $c)
      touch $F/KICK_MIX; say "$ARM static KICK_MIX switch at ts=$ts (protected copy ${ARM}_before_kickmix_$(basename $c)); restarting on the kick mix"
      stop_trainer || { say "ABORT: trainer would not stop"; exit 1; }
      start_trainer $ARM; switched=1; sleep 60; alive || { say "ERROR: $ARM did not come back after the switch"; exit 1; }
    fi
    [ -n "$ts" ] && [ "$ts" -ge "$END" ] && break
    alive || { sleep 120; alive || { say "ABORT: $ARM trainer died at ts=$ts (the wrapper did not restart it)"; exit 1; }; }
    sleep 30
  done
  stop_trainer || { say "ABORT: would not stop"; exit 1; }
  { echo "=== $ARM FINAL health at ${ts} ==="; TS2_ARM=$ARM OMP_NUM_THREADS=2 nice -n 19 python3 tools/gco_health/ts2_read.py; } >> $HL 2>&1
  c=$(ls -d $F/[0-9]* | sort -V | tail -1); cp -r $c $P/${ARM}_final_$(basename $c)
  say "ARCH $ARM DONE at ts=$ts; final copy $P/${ARM}_final_$(basename $c); kick-mix switched: $([ -f $F/KICK_MIX ] && echo yes || echo no)"
  ( cd build; cp GigaLearnBot h2h_eval.new && mv -f h2h_eval.new h2h_eval; GGL_H2H_GAMES=1024 OMP_NUM_THREADS=4 nice -n 19 python3 ../tools/gco_health/h2h_bots.py $ARM >> ../$ST 2>&1; echo "$(date '+%F %T') gauntlet-1024 $ARM finished" >> ../$ST ) &
done
wait
say "gauntlets done; running the cross-architecture head-to-head matrix"
python3 tools/gco_health/arch_h2h.py >> $ST 2>&1
say "ARCH ALL DONE"
