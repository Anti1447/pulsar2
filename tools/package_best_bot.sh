#!/usr/bin/env bash
# Package the STRONGEST-RATED checkpoint (best_r<rating>_<steps>) as a versioned RLBot v5 bot.
# Same options as package_bot.sh (--minor, --major, --dry-run, --no-install, --force, ...); see its header.
exec "$(dirname "$0")/package_bot.sh" --best "$@"
