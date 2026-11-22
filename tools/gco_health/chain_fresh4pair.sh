#!/bin/bash
# H-010 replicate pair (user-directed 2026-11-22): the original fork-point checkpoint rotated away, so both arms fork from fresh4's 1,678,274,560: fresh4c2 (heads coupled 0.1) then
# fresh4k2 (detached, coupling 0), both seed 456, +500M each, own folders. Waits for "NEXCHRON2 ALL DONE". Logs "FRESH4PAIR ALL DONE" to build/forks_status.log.
export PATH=/usr/local/bin:/usr/bin:/bin:$HOME/.local/bin:$PATH
cd /home/nhafe/pulsar2
ST=build/fresh4pair_status.log; HL=build/fresh4pair_health.log; FS=build/forks_status.log
say() { echo "$(date '+%F %T') $*" >> $ST; }
alive() { for p in /proc/[0-9]*/exe; do readlink $p 2>/dev/null | grep -q build/GigaLearnBot && return 0; done; return 1; }
lastts() { tail -1 "$1" 2>/dev/null | python3 -c "import sys,json;print(int(json.loads(sys.stdin.read())['ts']))" 2>/dev/null; }
stop_trainer() { tools/trainerctl stop >/dev/null 2>&1; for i in $(seq 1 100); do alive || return 0; sleep 3; done; return 1; }
START=1678274560; END=$(( START + 500000000 ))
run_arm() { # <flag> <folder> <reader>
  local flag=$1 folder=$2 reader=$3 next= ts L
  say "starting $flag"
  ( cd build && ../tools/run_trainer.sh --detach -- ./GigaLearnBot $flag >/dev/null 2>&1 )
  sleep 5; L=$(ls -t run_logs/train-*.log | head -1)
  for i in $(seq 1 120); do grep -q -E "Total Iterations|ERROR|Exception|terminate" $L && break; sleep 3; done
  if ! grep -q "Critic heads ENABLED" $L || ! grep -q "Loading checkpoint \"$folder/$START\"" $L; then
    say "ABORT: $flag did not load $folder/$START with heads on (log $L)"; return 1
  fi
  say "$flag booted  log $L"
  while true; do
    ts=$(lastts build/$folder/metrics.jsonl)
    [ -z "$next" ] && [ -n "$ts" ] && next=$(( ( ts / 100000000 + 1 ) * 100000000 ))
    if [ -n "$ts" ] && [ -n "$next" ] && [ "$ts" -ge "$next" ]; then
      { echo "=== $flag health at ${ts} ==="; OMP_NUM_THREADS=2 nice -n 19 python3 tools/gco_health/$reader; } >> $HL 2>&1
      next=$(( next + 100000000 ))
    fi
    [ -n "$ts" ] && [ "$ts" -ge "$END" ] && break
    alive || { say "ABORT: trainer died during $flag at ts=$ts"; return 1; }
    sleep 30
  done
  stop_trainer || { say "ABORT: $flag would not stop"; return 1; }
  say "DONE $flag at ts=$ts, final checkpoint $(ls -t build/$folder | grep -E '^[0-9]+$' | head -1)"
}
say "pair chain started; waiting for NEXCHRON2 ALL DONE"
until grep -qE " NEXCHRON2 ALL DONE$" $FS; do sleep 30; done
sleep 20; alive && { say "ABORT: trainer still alive after NEXCHRON2 ALL DONE"; exit 1; }
run_arm --gco-fresh4c2 checkpoints_gco_fresh4c2 fresh4c2_read.py || exit 1
run_arm --gco-fresh4k2 checkpoints_gco_fresh4k2 fresh4k2_read.py || exit 1
say "FRESH4PAIR ALL DONE"; echo "$(date '+%F %T') FRESH4PAIR ALL DONE" >> $FS
