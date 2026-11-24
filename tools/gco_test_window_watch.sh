#!/usr/bin/env bash
# Stops the GCO trainer when the critic-LR test window ends (docs/GCO_CRITIC_LR_TEST.md), then drops a flag file.
# Window = 300M steps from the restart at 58,065,933,072 -> 58,365,933,072.
TARGET=${1:-58365933072}
FLAG=/home/nhafe/pulsar2/GCO_TEST_DONE
LOG=/home/nhafe/pulsar2/run_logs/latest.log
while true; do
  f=$(readlink -f "$LOG")
  step=$(grep -a "Total Timesteps" "$f" 2>/dev/null | tail -1 | sed 's/.*: *//' | tr -d ', ')
  if [ -n "$step" ] && [ "$step" -ge "$TARGET" ]; then
    cd /home/nhafe/pulsar2/build && ../tools/run_trainer.sh --stop
    echo "step=$step target=$TARGET stopped=$(date -Is)" > "$FLAG"
    exit 0
  fi
  sleep 60
done
