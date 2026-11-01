#!/usr/bin/env bash
# Starts the GCO training dashboard on http://localhost:8767 (Ctrl+C stops the dashboard only).
cd "$(dirname "$0")" && exec python3 -u dashboard.py "$@"
