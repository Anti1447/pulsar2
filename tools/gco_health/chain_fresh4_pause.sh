#!/bin/bash
# Pauses the critic run (fresh4) cleanly once it reaches 1.0B steps: takes the pre-registered 1B health read, stops the trainer, logs "FRESH4 PAUSED" to build/forks_status.log.
# Then the external-opponent arm chains (chain_arm.sh) run, and chain_fresh4_resume.sh resumes fresh4. Launch: systemd-run --user --unit=gco-fresh4-pause tools/gco_health/chain_fresh4_pause.sh
export PATH=/usr/local/bin:/usr/bin:/bin:$HOME/.local/bin:$PATH
cd /home/nhafe/pulsar2
ST=build/forks_status.log; HL=build/fresh4_health.log
say() { echo "$(date '+%F %T') $*" >> $ST; }
alive() { for p in /proc/[0-9]*/exe; do readlink $p 2>/dev/null | grep -q build/GigaLearnBot && return 0; done; return 1; }
lastts() { tail -1 "$1" 2>/dev/null | python3 -c "import sys,json;print(int(json.loads(sys.stdin.read())['ts']))" 2>/dev/null; }
say "fresh4 pause chain started; will pause at 1.0B"
while true; do ts=$(lastts build/checkpoints_gco_fresh4/metrics.jsonl); [ -n "$ts" ] && [ "$ts" -ge 1000000000 ] && break; alive || { say "ABORT pause: fresh4 not running at ts=$ts"; exit 1; }; sleep 30; done
{ echo "=== fresh4 health at ${ts} (1B read, pause) ==="; OMP_NUM_THREADS=2 nice -n 19 python3 tools/gco_health/fresh4_read.py; } >> $HL 2>&1
tools/trainerctl stop >/dev/null 2>&1; for i in $(seq 1 100); do alive || break; sleep 3; done
alive && { say "ABORT pause: fresh4 would not stop"; exit 1; }
say "fresh4 paused at ts=$ts, last checkpoint $(ls -t build/checkpoints_gco_fresh4 | grep -E '^[0-9]+$' | head -1)"
say "FRESH4 PAUSED"
