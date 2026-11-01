@echo off
rem Starts the Pulsar2 GCO dashboard inside WSL and opens it in your browser. Closing this window stops the dashboard only.
title Pulsar2 GCO dashboard
start "" http://localhost:8767
wsl.exe -d Ubuntu-24.04 -u nhafe -- /home/nhafe/pulsar2/tools/dashboard_gco/run_dashboard.sh %*
