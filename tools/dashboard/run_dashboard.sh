#!/usr/bin/env bash
# Starts the live training dashboard on http://localhost:8765 (Ctrl+C stops the dashboard only).
cd "$(dirname "$0")" && exec python3 -u dashboard.py "$@"
