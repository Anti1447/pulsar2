#!/bin/bash
# usage: eval_arm.sh <arm>. Waits for "<ARM> ALL DONE" in build/forks_status.log, then the standard battery on that arm: head-to-head vs ctrl, ctrl2, kick, start (128 arenas) and the
# 1024-game external-bot gauntlet. Logs "<ARM> EVALS DONE", or "<ARM> EVALS FAILED" if either driver exits nonzero. CPU only, niced. Needs build/h2h_eval to be a copy of the current binary (launch_arm.py refreshes it).
export PATH=/usr/local/bin:/usr/bin:/bin:$HOME/.local/bin:$PATH
arm=$1; U=$(echo $arm | tr a-z A-Z)
cd /home/nhafe/pulsar2/build
until grep -qE " $U ALL DONE$" forks_status.log; do sleep 60; done
export GGL_H2H_GAMES=128
ok=1
python3 ../tools/gco_health/h2h_matrix.py $arm:ctrl,ctrl2,kick,start || ok=0
GGL_H2H_GAMES=1024 python3 ../tools/gco_health/h2h_bots.py $arm || ok=0
if [ $ok = 1 ]; then echo "$(date '+%F %T') $U EVALS DONE" >> forks_status.log; else echo "$(date '+%F %T') $U EVALS FAILED (see journal)" >> forks_status.log; fi
