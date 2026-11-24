#!/bin/bash
# G1 Part B: 6 cells x {cpu, gpu}. Starts are sampled once per (mode) by the cpu arm and shared with the gpu arm.
# usage: run_part_b.sh [episodes]   Output: parity_out/<mode>_<matchup>_<arm>.csv   (run as a systemd --user unit; niced)
cd "$(dirname "$0")/../../build-play" || exit 1
EP=${1:-20000}; K=${K:-512}
for mode in 1v1 2v2 3v3; do
  for matchup in self vs_oldest; do
    if [ $matchup = self ]; then ORANGE=../parity_ckpt/latest; else ORANGE=../parity_ckpt/oldest; fi
    for arm in cpu gpu; do
      out=../parity_out/${mode}_${matchup}_${arm}.csv
      [ -f $out ] && { echo "skip $out"; continue; }
      echo "=== $mode $matchup $arm $(date +%T)"
      nice -n 10 ./ParityPlay --arm $arm --mode $mode --episodes $EP --arenas $K --blue ../parity_ckpt/latest --orange $ORANGE --starts ../parity_out/${mode}_${matchup}.starts --out $out 2>&1 | tr '\r' '\n' | tail -n 2
    done
  done
done
echo ALL-DONE $(date +%T)
