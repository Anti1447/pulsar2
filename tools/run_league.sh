#!/usr/bin/env bash
# Main training plus both exploiter slots on one machine (docs/EXPLOITERS.md). Run from the build directory:
#
#   ../tools/run_league.sh start     main (--together: half of numGames) + slot 0 (main exploiter) + slot 1 (league)
#   ../tools/run_league.sh status
#   ../tools/run_league.sh stop      each saves after its current iteration (exploiters run a final eval first)
#
# Each piece is its own run_trainer.sh wrapper with its own log dir, so they restart and stop independently:
# main logs to run_logs/ (what the dashboard reads), slot N to run_logs/exploiter_slot<N>/.

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SLOTS="0 1"

case "${1:-}" in
	start)
		"$SCRIPT_DIR/run_trainer.sh" -- ./GigaLearnBot --together
		for s in $SLOTS; do
			"$SCRIPT_DIR/run_exploiter.sh" --slot "$s"
		done
		;;
	status)
		echo "== main =="; "$SCRIPT_DIR/run_trainer.sh" --status
		for s in $SLOTS; do
			echo "== exploiter slot $s =="; "$SCRIPT_DIR/run_exploiter.sh" --slot "$s" --status
		done
		;;
	stop)
		for s in $SLOTS; do
			"$SCRIPT_DIR/run_exploiter.sh" --slot "$s" --stop &
		done
		"$SCRIPT_DIR/run_trainer.sh" --stop
		wait
		;;
	*)
		sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'
		exit 2
		;;
esac
