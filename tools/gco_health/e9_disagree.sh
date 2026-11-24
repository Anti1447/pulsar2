#!/bin/bash
# E9 (docs/GCO_IMITATION_CHRONOS.md section 11): Chronos vs Galactus on the same states. CPU, niced. Output build/e9_disagree.jsonl
cd /home/nhafe/pulsar2/build
OMP_NUM_THREADS=4 OPENBLAS_NUM_THREADS=4 GGL_DEVICE=cpu GGL_H2H_BOTS=1 GGL_H2H_DISAGREE=1 GGL_H2H_NATIVE=1 GGL_H2H_GAMES=${GAMES:-512} GGL_H2H_SEED=${SEED:-777001} nice -n 19 ./h2h_eval --gco-fork-nexchron 2>&1 | grep DISAGREE_RESULT | sed 's/^DISAGREE_RESULT //' >> e9_disagree.jsonl
echo done >> e9_disagree.done
