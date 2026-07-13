#!/bin/sh
# Starts the background services the simulation needs.
#
# veins_launchd brokers TraCI: OMNeT++ connects to it on port 9999 and it spawns a
# private SUMO instance per run. Without it every run dies at t=0 with
# "Could not connect to TraCI server: Connection refused".

PORT=9999
THESISWORK=/home/jhuang/thesiswork

if (ss -ltn 2>/dev/null || netstat -ltn 2>/dev/null) | grep -q ":$PORT "; then
    echo "veins_launchd already listening on $PORT"
    exit 0
fi

cd "$THESISWORK" || exit 1
nohup python3 veins-git/bin/veins_launchd -p "$PORT" > /tmp/veins_launchd.log 2>&1 &
sleep 2

if (ss -ltn 2>/dev/null || netstat -ltn 2>/dev/null) | grep -q ":$PORT "; then
    echo "veins_launchd started on $PORT"
else
    echo "veins_launchd FAILED to start; see /tmp/veins_launchd.log" >&2
    exit 1
fi
