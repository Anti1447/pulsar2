#!/bin/bash
# usage: wait3.sh <target steps in millions>  - waits for fresh3 to reach it, then prints fresh3_read.py
T=$(( $1 * 1000000 ))
while true; do
  ts=$(tail -1 /home/nhafe/pulsar2/build/checkpoints_gco_fresh3/metrics.jsonl 2>/dev/null | python3 -c "import sys,json;print(int(json.loads(sys.stdin.read())['ts']))" 2>/dev/null)
  [ -n "$ts" ] && [ "$ts" -ge "$T" ] && break
  alive=0; for p in /proc/[0-9]*/exe; do readlink $p 2>/dev/null | grep -q build/GigaLearnBot && alive=1; done
  [ $alive = 0 ] && { echo "TRAINER NOT RUNNING at ts=$ts"; exit 0; }
  sleep 60
done
echo "=== fresh3 health read at $1M ==="
OMP_NUM_THREADS=2 nice -n 19 python3 /home/nhafe/pulsar2/tools/gco_health/fresh3_read.py
