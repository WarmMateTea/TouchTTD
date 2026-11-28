#!/usr/bin/env bash
# Stop the OpenTTD and Xvfb started by run_headless.sh.
# Every headless OpenTTD using the private config is stopped, whatever its display: games left running eat the CPU.
DISPLAY_NUM=${DISPLAY_NUM:-99}
CFG="${WORK:-/tmp/ttd-headless}/openttd.cfg"
pids() { ps -C openttd -o pid=,args= | awk -v cfg="$CFG" 'index($0, "-c " cfg) { print $1 }'; }
P=$(pids); [ -n "$P" ] && kill $P 2>/dev/null
for _ in 1 2 3 4 5; do [ -z "$(pids)" ] && break; sleep 0.5; done
P=$(pids); [ -n "$P" ] && kill -9 $P 2>/dev/null
pkill -f "Xvfb :$DISPLAY_NUM " 2>/dev/null || true
exit 0
