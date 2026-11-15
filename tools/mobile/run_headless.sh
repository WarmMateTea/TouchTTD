#!/usr/bin/env bash
# Run OpenTTD on a virtual X display (Xvfb) at a phone-like resolution, for screenshots without a monitor.
#
# Usage: tools/mobile/run_headless.sh WIDTH HEIGHT [GUI_SCALE] [MOBILE_LAYOUT] [extra openttd args...]
#   tools/mobile/run_headless.sh 2714 1222            # the reference phone, landscape, 300%, mobile layout
#   tools/mobile/run_headless.sh 1222 2714            # same phone, portrait
#   tools/mobile/run_headless.sh 1920 1080 200 false  # desktop layout at 200%
#
# Environment: DISPLAY_NUM (default 99), WORK (default /tmp/ttd-headless), OPENTTD (default build/openttd),
#              START=menu to start at the main menu instead of a new game.
# Drive it with:  DISPLAY=:99 xdotool mousemove X Y click 1
# Screenshot:     DISPLAY=:99 import -window root shot.png
# Stop:           tools/mobile/stop_headless.sh
set -euo pipefail

W=${1:?width}; H=${2:?height}; SCALE=${3:-300}; MOBILE=${4:-true}; shift $(( $# < 4 ? $# : 4 ))
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DISPLAY_NUM=${DISPLAY_NUM:-99}
WORK=${WORK:-/tmp/ttd-headless}
OPENTTD=${OPENTTD:-$ROOT/build/openttd}
mkdir -p "$WORK"

# A private config, so the user's own openttd.cfg is never touched.
cat > "$WORK/openttd.cfg" <<CFG
[misc]
resolution = $W,$H
fullscreen = false
gui_scale = $SCALE
[gui]
mobile_layout = $MOBILE
scroll_mode = 3
autosave = off
[network]
participate_survey = no
CFG

pkill -f "Xvfb :$DISPLAY_NUM" 2>/dev/null || true
Xvfb ":$DISPLAY_NUM" -screen 0 "${W}x${H}x24" -nolisten tcp >"$WORK/xvfb.log" 2>&1 &
sleep 1
# -g: start a new random game, -G: fixed seed so every run gets the same map, -x: never save the config.
START_ARGS=(-g -G 12345)
[ "${START:-game}" = menu ] && START_ARGS=()
DISPLAY=":$DISPLAY_NUM" "$OPENTTD" -c "$WORK/openttd.cfg" -x "${START_ARGS[@]}" -m null -s null -v sdl "$@" >"$WORK/openttd.log" 2>&1 &
echo "OpenTTD started on :$DISPLAY_NUM (${W}x${H}, ${SCALE}%, mobile_layout=$MOBILE). Log: $WORK/openttd.log"
