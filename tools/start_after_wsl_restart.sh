#!/usr/bin/env bash
# Started from Windows (C:\Users\<you>\pulsar_restart\restart_wsl.cmd) right after "wsl --shutdown": starts Pulsar3 again,
# records RAM use, then stays running so WSL is not shut down for being idle. "check" does everything except start and stay.
P="$(cd "$(dirname "$0")/.." && pwd)"
LOG="$P/run_logs/wsl_restart.log"
export XDG_RUNTIME_DIR="/run/user/$(id -u)"
say() { echo "$(date '+%F %T') $*" | tee -a "$LOG"; }
trainer_up() { for p in /proc/[0-9]*; do case "$(readlink "$p/exe" 2>/dev/null)" in "$P/build/GigaLearnBot"*) return 0 ;; esac; done; return 1; }
ram() { free -m | awk '/^Mem:/ {printf "WSL RAM used %d MB, cache %d MB, available %d MB", $3, $6, $7}'; }

state=
for i in $(seq 1 60); do
	state="$(systemctl --user is-system-running 2>/dev/null)"
	case "$state" in running|degraded) break ;; esac
	sleep 2
done
say "user systemd: ${state:-not reachable}; $(ram)"
if trainer_up; then
	say "Pulsar3 is already running, not starting a second one"
elif [ "$1" = check ]; then
	say "check: trainer not running; would start it"
else
	(cd "$P/build" && ../tools/run_trainer.sh --detach -- ./GigaLearnBot --pulsar3) 2>&1 | tee -a "$LOG"
fi
[ "$1" = check ] && exit 0
sleep 120
if trainer_up; then say "Pulsar3 up after 2 minutes; $(ram)"; else say "Pulsar3 NOT running after 2 minutes"; fi
exec sleep infinity
