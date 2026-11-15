#!/usr/bin/env bash
# Take a screenshot of the headless display: tools/mobile/shot.sh OUT.png [scale%]
# The full-resolution PNG goes to OUT.png; with a scale, a smaller copy goes to OUT-small.png.
set -euo pipefail
OUT=${1:?output png}; SCALE=${2:-}
sleep "${SHOT_DELAY:-0.6}"
DISPLAY=":${DISPLAY_NUM:-99}" import -window root "$OUT"
if [ -n "$SCALE" ]; then convert "$OUT" -resize "$SCALE%" "${OUT%.png}-small.png"; fi
