@echo off
rem Starts the Pulsar2 dashboard inside WSL and opens it in your browser. Closing this window stops the dashboard only.
title Pulsar2 dashboard
start "" http://localhost:8766
wsl.exe -d Ubuntu-24.04 -u nhafe -- /home/nhafe/pulsar2/tools/dashboard/run_dashboard.sh %*
