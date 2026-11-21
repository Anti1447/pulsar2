#!/bin/bash
# Evals for the external-opponent arms: head-to-head vs ctrl/ctrl2/kick/start and each other, then the 1024-game external-bot gauntlet. Runs the three finished arms now,
# waits for "DICE ALL DONE" in build/forks_status.log, then dice. Results: build/h2h_results.jsonl, build/gaunt_hp_results.jsonl. Launch: nohup tools/gco_health/eval_extopp.sh &
export PATH=/usr/local/bin:/usr/bin:/bin:$HOME/.local/bin:$PATH
cd /home/nhafe/pulsar2/build; M=../tools/gco_health/h2h_matrix.py; G=../tools/gco_health/h2h_bots.py
export GGL_H2H_GAMES=128
python3 $M nexto:ctrl,ctrl2,kick,start chronos:ctrl,ctrl2,kick,start galactus:ctrl,ctrl2,kick,start nexto:chronos,galactus chronos:galactus
GGL_H2H_GAMES=1024 python3 $G nexto chronos galactus
until grep -qE " DICE ALL DONE$" forks_status.log; do sleep 60; done
python3 $M dice:ctrl,ctrl2,kick,start,nexto,chronos,galactus
GGL_H2H_GAMES=1024 python3 $G dice
echo "$(date '+%F %T') EXTOPP EVALS DONE" >> forks_status.log
