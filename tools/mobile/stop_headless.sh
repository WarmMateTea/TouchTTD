#!/usr/bin/env bash
# Stop the OpenTTD and Xvfb started by run_headless.sh.
DISPLAY_NUM=${DISPLAY_NUM:-99}
pkill -f "openttd -c ${WORK:-/tmp/ttd-headless}/openttd.cfg" 2>/dev/null || true
pkill -f "Xvfb :$DISPLAY_NUM" 2>/dev/null || true
