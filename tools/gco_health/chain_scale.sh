#!/bin/bash
# H-013 scale run (user-directed 2026-11-22): continue arm nexchron2 (Nexto+Chronos 30%, control mix, seed 456) from its final checkpoint 5,499,959,296 for +1.0B steps IN ITS OWN FOLDER.
# Protected copies (outside the rotation): build/protected_scale/nexchron2_5499959296 (start) and ..._mid (at +500M). Health reads every 250M to build/scale_health.log.
# At the end: head-to-head of the final checkpoint vs start, mid, ctrl, ctrl2, kick, nexchron, run-1 start (128 arenas, to build/h2h_scale.jsonl) and the 1024-game gauntlet. Logs "SCALE DONE".
export PATH=/usr/local/bin:/usr/bin:/bin:$HOME/.local/bin:$PATH
cd /home/nhafe/pulsar2
ST=build/scale_status.log; HL=build/scale_health.log; P=build/protected_scale
say() { echo "$(date '+%F %T') $*" >> $ST; }
alive() { for p in /proc/[0-9]*/exe; do readlink $p 2>/dev/null | grep -q build/GigaLearnBot && return 0; done; return 1; }
lastts() { tail -1 "$1" 2>/dev/null | python3 -c "import sys,json;print(int(json.loads(sys.stdin.read())['ts']))" 2>/dev/null; }
stop_trainer() { tools/trainerctl stop >/dev/null 2>&1; for i in $(seq 1 100); do alive || return 0; sleep 3; done; return 1; }
START=5499959296; END=$(( START + 1000000000 )); F=build/checkpoints_gco_fork_nexchron2
alive && { say "ABORT: a trainer is running"; exit 1; }
say "scale run starting from nexchron2 $START"
( cd build && ../tools/run_trainer.sh --detach -- ./GigaLearnBot --gco-fork-nexchron2 >/dev/null 2>&1 )
sleep 5; L=$(ls -t run_logs/train-*.log | head -1)
for i in $(seq 1 120); do grep -q -E "Total Iterations|ERROR|Exception|terminate" $L && break; sleep 3; done
grep -q "Loading checkpoint \"checkpoints_gco_fork_nexchron2/" $L || { say "ABORT: did not load nexchron2 (log $L)"; exit 1; }
say "booted: $(grep -m1 'Loading checkpoint' $L | cut -c1-100) log $L"
next=$(( START + 250000000 )); midcopied=0
while true; do
  ts=$(lastts $F/metrics.jsonl)
  if [ -n "$ts" ] && [ "$ts" -ge "$next" ]; then
    { echo "=== scale health at ${ts} (+$(( (ts-START)/1000000 ))M) ==="; OMP_NUM_THREADS=2 nice -n 19 python3 tools/gco_health/fork_read.py nexchron2; } >> $HL 2>&1
    next=$(( next + 250000000 ))
  fi
  if [ -n "$ts" ] && [ "$midcopied" = 0 ] && [ "$ts" -ge $(( START + 500000000 )) ]; then
    c=$(ls -d $F/[0-9]* | sort -V | tail -1); cp -r $c $P/nexchron2_mid_$(basename $c) && midcopied=1 && say "mid copy $c"
  fi
  [ -n "$ts" ] && [ "$ts" -ge "$END" ] && break
  alive || { say "ABORT: trainer died at ts=$ts"; exit 1; }
  sleep 30
done
stop_trainer || { say "ABORT: would not stop"; exit 1; }
FINAL=$(ls -d $F/[0-9]* | sort -V | tail -1); say "DONE at ts=$ts, final $FINAL"
cd build; cp GigaLearnBot h2h_eval.new && mv -f h2h_eval.new h2h_eval
MID=$(ls -d $PWD/protected_scale/nexchron2_mid_* | head -1)
OPPS="$PWD/protected_scale/nexchron2_5499959296,$MID,$PWD/checkpoints_gco_fork_ctrl/5497937920,$PWD/checkpoints_gco_fork_ctrl2/5497100288,$PWD/checkpoints_gco_fork_kick/5501415424,$PWD/checkpoints_gco_fork_nexchron/5501441024,$PWD/checkpoints_gco_fresh_FINAL_4995887104_BACKUP"
GGL_H2H_GAMES=128 GGL_H2H_OPP=$OPPS OMP_NUM_THREADS=4 GGL_DEVICE=cpu nice -n 19 ./h2h_eval --gco-fork-nexchron2 2>&1 | grep H2H_RESULT >> h2h_scale.jsonl
GGL_H2H_GAMES=1024 python3 ../tools/gco_health/h2h_bots.py nexchron2 >> scale_status.log 2>&1
say "SCALE DONE"
