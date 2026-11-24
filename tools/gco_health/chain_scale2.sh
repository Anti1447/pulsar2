#!/bin/bash
# H-013 scale run, user-directed 2026-11-22: the trainer is NEVER stopped. Replaces chain_scale.sh (which stopped at +1.0B).
# At START+1.0B: snapshot the newest numbered checkpoint to build/protected_scale, run the H-013 evals on that SNAPSHOT in a symlinked eval dir
# (niced CPU, concurrent with training), log SCALE DONE. Then keep reading health every 250M and snapshot every +500M, indefinitely.
export PATH=/usr/local/bin:/usr/bin:/bin:$HOME/.local/bin:$PATH
cd /home/nhafe/pulsar2
ST=build/scale_status.log; HL=build/scale_health.log; P=build/protected_scale
say() { echo "$(date '+%F %T') $*" >> $ST; }
alive() { for p in /proc/[0-9]*/exe; do readlink $p 2>/dev/null | grep -q build/GigaLearnBot && return 0; done; return 1; }
lastts() { tail -1 "$1" 2>/dev/null | python3 -c "import sys,json;print(int(json.loads(sys.stdin.read())['ts']))" 2>/dev/null; }
START=5499959296; END=$(( START + 1000000000 )); F=build/checkpoints_gco_fork_nexchron2
say "chain_scale2 attached (trainer keeps running indefinitely)"
next=$(( START + 1000000000 )); snapnext=$(( START + 1500000000 )); evaldone=0
while true; do
  ts=$(lastts $F/metrics.jsonl)
  if [ -n "$ts" ] && [ "$ts" -ge "$(( START + 750000000 ))" ] && [ "${h750:-0}" = 0 ]; then
    { echo "=== scale health at ${ts} (+$(( (ts-START)/1000000 ))M) ==="; OMP_NUM_THREADS=2 nice -n 19 python3 tools/gco_health/fork_read.py nexchron2; } >> $HL 2>&1; h750=1
  fi
  if [ -n "$ts" ] && [ "$ts" -ge "$next" ]; then
    { echo "=== scale health at ${ts} (+$(( (ts-START)/1000000 ))M) ==="; OMP_NUM_THREADS=2 nice -n 19 python3 tools/gco_health/fork_read.py nexchron2; } >> $HL 2>&1
    next=$(( next + 250000000 ))
  fi
  if [ -n "$ts" ] && [ "$evaldone" = 0 ] && [ "$ts" -ge "$END" ]; then
    c=$(ls -d $F/[0-9]* | sort -V | tail -1); S=$P/nexchron2_final_$(basename $c); cp -r $c $S; say "H-013 final snapshot $c -> $S at ts=$ts"
    E=build/evalrun; rm -rf $E; mkdir -p $E/checkpoints_gco_fork_nexchron2
    for x in build/*; do b=$(basename $x); [ "$b" = checkpoints_gco_fork_nexchron2 ] || [ "$b" = evalrun ] || ln -s $PWD/$x $E/$b; done
    cp -r $S $E/checkpoints_gco_fork_nexchron2/$(basename $c)
    cp build/GigaLearnBot $E/h2h_eval.new && mv -f $E/h2h_eval.new $E/h2h_eval; rm -f $E/h2h_eval_link
    MID=$(ls -d $PWD/build/protected_scale/nexchron2_mid_* | head -1); B=$PWD/build
    OPPS="$B/protected_scale/nexchron2_5499959296,$MID,$B/checkpoints_gco_fork_ctrl/5497937920,$B/checkpoints_gco_fork_ctrl2/5497100288,$B/checkpoints_gco_fork_kick/5501415424,$B/checkpoints_gco_fork_nexchron/5501441024,$B/checkpoints_gco_fresh_FINAL_4995887104_BACKUP"
    ( cd $E; GGL_H2H_GAMES=128 GGL_H2H_OPP=$OPPS OMP_NUM_THREADS=4 GGL_DEVICE=cpu nice -n 19 ./h2h_eval --gco-fork-nexchron2 2>&1 | grep H2H_RESULT >> ../h2h_scale.jsonl )
    ( cd $E; sed "s#R='/home/nhafe/pulsar2/build/'#R='$PWD/'#; s#OUT=R+'gaunt_hp_results.jsonl'#OUT='$B/gaunt_hp_results.jsonl'#" ../../tools/gco_health/h2h_bots.py > h2h_bots_eval.py; GGL_H2H_GAMES=1024 python3 h2h_bots_eval.py nexchron2 >> ../scale_status.log 2>&1 )
    say "SCALE DONE"; evaldone=1
  fi
  if [ -n "$ts" ] && [ "$ts" -ge "$snapnext" ]; then
    c=$(ls -d $F/[0-9]* | sort -V | tail -1); cp -r $c $P/nexchron2_snap_$(basename $c) && say "snapshot $c at ts=$ts"; snapnext=$(( snapnext + 500000000 ))
  fi
  sleep 60
done
