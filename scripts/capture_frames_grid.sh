#!/usr/bin/env bash
# capture_frames_grid.sh - capture N frames of a running GUI app over time and
# tile them into ONE contact-sheet image, so a vision model can verify MOTION:
# moving characters, camera scrolling, animation, HUD counters ticking.
#
# Why: a single static capture (capture_for_vision.sh / verify_gui_render.sh)
# proves the layout renders but cannot show that anything moves or animates.
# Games and simulations need the time dimension. One tiled image also costs
# one vision call instead of N.
#
# Usage:
#   bash capture_frames_grid.sh <binary> [window-title] [frames=4] [interval_s=2] [wait_first=3]
#
# Output:
#   /tmp/imgui_vision/<name>_frames.png   N frames tiled in a row (each
#                                         downscaled to 512px wide, timestamp
#                                         label baked in)
#
# Requires: xdotool + imagemagick (import/convert/montage) + an X display.
# NOTE: kills pre-existing instances of the binary first (determinism guard).

set -euo pipefail

BIN="${1:?usage: capture_frames_grid.sh <binary> [window-title] [frames] [interval_s] [wait_first]}"
TITLE="${2:-}"
N="${3:-4}"
INTERVAL="${4:-2}"
WAIT="${5:-3}"
export DISPLAY="${DISPLAY:-:1}"

for tool in xdotool import convert montage; do
    command -v "$tool" >/dev/null 2>&1 || { echo "missing tool: $tool"; exit 2; }
done
if ! xdpyinfo >/dev/null 2>&1; then
    echo "no X display on DISPLAY=$DISPLAY"; exit 3
fi

OUTDIR=/tmp/imgui_vision
mkdir -p "$OUTDIR"
NAME="$(basename "$BIN" | tr ' .' '__')"

echo "== launching $BIN =="
pkill -xf "$BIN" 2>/dev/null || true   # determinism: no concurrent same-binary instances (-xf: don't match this script's own cmdline)
sleep 0.5
"$BIN" &
APP_PID=$!
cleanup() { kill "$APP_PID" 2>/dev/null || true; }
trap cleanup EXIT

sleep "$WAIT"
kill -0 "$APP_PID" 2>/dev/null || { echo "FAIL: app exited early"; exit 4; }

WID=""
if [[ -n "$TITLE" ]]; then
    WID="$(xdotool search --onlyvisible --name "$TITLE" 2>/dev/null | head -1 || true)"
fi
[[ -n "$WID" ]] || WID="$(xdotool search --onlyvisible --name "." 2>/dev/null | tail -1 || true)"
[[ -n "$WID" ]] || { echo "FAIL: no visible window"; exit 5; }

FRAMES=""
for i in $(seq 1 "$N"); do
    F="$OUTDIR/${NAME}_f$i.png"
    import -window "$WID" "$F"
    convert "$F" -resize 512x -bordercolor lime -border 2 \
        -gravity southeast -pointsize 24 -fill white \
        -annotate +8+8 "t+$(( WAIT + (i-1) * INTERVAL ))s" "$F"
    FRAMES="$FRAMES $F"
    echo "captured frame $i/$N"
    [[ "$i" -lt "$N" ]] && sleep "$INTERVAL"
done

GRID="$OUTDIR/${NAME}_frames.png"
montage $FRAMES -tile "${N}x1" -geometry +4+4 -background black "$GRID"

echo ""
echo "==============================================================="
echo "CONTACT SHEET: $(realpath "$GRID")  ($N frames, ${INTERVAL}s apart)"
echo "Pass it to the vision tool with a MOTION question, e.g.:"
echo '  vision_analyze(image_url="<path>", question='
echo '    "These are N frames of the same game window taken over time.'
echo '     1) Is anything moving/changing between frames (player, enemies,'
echo '     camera scroll, HUD counters)? 2) Is movement consistent with'
echo '     the game controls? 3) Any frames identical (frozen sim)?")'
echo "==============================================================="
