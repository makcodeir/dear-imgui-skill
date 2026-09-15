#!/usr/bin/env bash
# capture_for_vision.sh - launch a GUI ImGui app, screenshot its window, and
# prepare the capture so a vision model can inspect it (agent-in-the-loop QA).
#
# Why: a GUI can compile clean, exit 0, and still have broken layout (panels
# pushed off-window, clipped text, invisible widgets). Histograms prove colours
# exist but not THAT THE LAYOUT IS RIGHT. Only a vision model reading the
# actual pixels closes that gap - see verify_gui_render.sh for the histogram
# half; this is the "look at it" half.
#
# Usage:
#   bash capture_for_vision.sh <binary> [window-title-substring] [wait-seconds] [app-args...]
#   (anything after the first three positional args is passed to the app,
#    e.g. a file to open: ./notepad /tmp/memo.txt)
#
# Output:
#   /tmp/imgui_vision/<name>_full.png   full-resolution capture
#   /tmp/imgui_vision/<name>_small.png  width-1024 copy (cheaper tokens,
#                                       fine for layout questions)
# The script PRINTS the exact paths; pass the small one to the vision tool.
#
# Requires: xdotool + imagemagick (import/convert) and an X display.

set -euo pipefail

BIN="${1:?usage: capture_for_vision.sh <binary> [window-title-substring] [wait-seconds] [app-args...]}"
TITLE="${2:-}"
WAIT="${3:-3}"
# Remaining args go to the app (e.g. a file to open).
if [ "$#" -ge 4 ]; then
    shift 3
else
    set --
fi
APP_ARGS=("$@")
export DISPLAY="${DISPLAY:-:1}"

for tool in xdotool import convert; do
    command -v "$tool" >/dev/null 2>&1 || { echo "missing tool: $tool (install xdotool + imagemagick)"; exit 2; }
done

OUTDIR=/tmp/imgui_vision
mkdir -p "$OUTDIR"
NAME="$(basename "$BIN" | tr ' .' '__')"

if ! xdpyinfo >/dev/null 2>&1; then
    echo "no X display on DISPLAY=$DISPLAY - cannot capture; use the headless gate instead"
    exit 3
fi

echo "== launching $BIN ${APP_ARGS[*]:-} on DISPLAY=$DISPLAY =="
# Determinism guard: a previous instance of the SAME binary still running
# (e.g. from an earlier capture or manual test) silently corrupts comparisons
# - two apps tick the sim, so replay frame counts/scores won't match the
# headless selftest. Kill same-binary instances first (hit live in the
# platformer session: GUI 1424 frames vs selftest 1242).
pkill -xf "$BIN" 2>/dev/null || true
sleep 0.5
"${BIN}" "${APP_ARGS[@]}" &
APP_PID=$!
cleanup() { kill "$APP_PID" 2>/dev/null || true; }
trap cleanup EXIT

sleep "$WAIT"
if ! kill -0 "$APP_PID" 2>/dev/null; then
    echo "FAIL: app exited within ${WAIT}s (no window to capture)"
    exit 4
fi

WID=""
if [[ -n "$TITLE" ]]; then
    WID="$(xdotool search --onlyvisible --name "$TITLE" 2>/dev/null | head -1 || true)"
fi
[[ -n "$WID" ]] || WID="$(xdotool search --onlyvisible --name "." 2>/dev/null | tail -1 || true)"
[[ -n "$WID" ]] || { echo "FAIL: no visible window found"; exit 5; }

FULL="$OUTDIR/${NAME}_full.png"
SMALL="$OUTDIR/${NAME}_small.png"
import -window "$WID" "$FULL"
convert "$FULL" -resize 1024x "$SMALL"

echo ""
echo "==============================================================="
echo "CAPTURE READY - inspect with the vision tool, e.g. in Hermes:"
echo "  vision_analyze(image_url=\"$(realpath "$SMALL")\", question=<below>)"
echo "full-res: $(realpath "$FULL")"
echo "small   : $(realpath "$SMALL")"
echo "==============================================================="
echo ""
echo "Suggested question (edit to name YOUR app's expected layout):"
cat <<'EOF'
  "Describe this UI precisely: 1) Which panels/regions are visible and where
   (top toolbar, left list, center canvas, right panel, bottom bar)?
   2) Are the expected widgets/objects present in each region?
   3) Is any text clipped, overlapping, or pushed off-window?
   4) Any visual glitches (stray artifacts, empty panels, missing content)?"
EOF
