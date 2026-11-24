#!/bin/bash
# usage: TEACHER=<bot name> e0_teacher.sh (default ChronosV2); output build/e0_teacher_<name>.jsonl
# E0 (docs/GCO_IMITATION_CHRONOS.md): the teacher's own noise floor. ChronosV2 as the subject vs every external bot incl. itself, at its trained cadence (tickSkip 8, delay 7), two seeds, 1024 games each. CPU, niced.
cd /home/nhafe/pulsar2/build
for seed in 777001 777002; do
  OMP_NUM_THREADS=4 OPENBLAS_NUM_THREADS=4 GGL_DEVICE=cpu GGL_H2H_BOTS=1 GGL_H2H_TEACHER=${TEACHER:-ChronosV2} GGL_H2H_NATIVE=1 GGL_H2H_GAMES=1024 GGL_H2H_SEED=$seed nice -n 19 ./h2h_eval --gco-fork-nexchron 2>&1 | grep GAUNT_RESULT | sed 's/^GAUNT_RESULT //' >> e0_teacher_${TEACHER:-ChronosV2}.jsonl
done
echo done >> e0_teacher_${TEACHER:-ChronosV2}.done
