#!/bin/bash
# Light mode for Pulsar3: leave most of the GPU and CPU to you. Takes effect within seconds, no restart.
#   tools/light.sh on [share]    share of time the trainer works, 0.05-1, default 0.25
#   tools/light.sh off           full speed
#   tools/light.sh status
F="$(cd "$(dirname "$0")/.." && pwd)/build/checkpoints_pulsar3/THROTTLE"
case "$1" in
	on)
		share=${2:-0.25}
		awk -v s="$share" 'BEGIN { exit !(s + 0 >= 0.05 && s + 0 <= 1) }' || { echo "share must be between 0.05 and 1"; exit 2; }
		echo "$share" > "$F" && echo "light mode on: the trainer works about $share of the time" ;;
	off) rm -f "$F" && echo "light mode off: full speed" ;;
	status) echo "share: $(cat "$F" 2>/dev/null || echo 1)" ;;
	*) echo "usage: light.sh on [share] | off | status"; exit 2 ;;
esac
