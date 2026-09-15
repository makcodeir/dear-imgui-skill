#!/usr/bin/env bash
# Launch a GUI ImGui app, screenshot its window, and dump a colour histogram so
# you can verify it ACTUALLY RENDERS — programmatically, no eyeballs required.
#
# Why this exists: the headless self-test (-DHEADLESS_SELFTEST) proves your API
# usage is correct, but it does NOT prove the window draws anything. A GUI can
# compile clean, exit 0, and still paint an empty frame. This closes that gap on
# a machine with a real or virtual X display.
#
# Usage:
#   bash verify_gui_render.sh <binary> [window-title-substring] [wait-seconds]
#
# Example:
#   bash verify_gui_render.sh ./blueprint_node_graph "Blueprint" 4
#
# Then check the histogram for the specific colours your draw code emits. If the
# palette you expect (header bars, canvas bg, pin colours) is present with
# sensible pixel counts, the render is real. If the histogram only shows desktop
# colours, you captured the wrong window or the app didn't map a window.
#
# Requires: xdotool + imagemagick (import/convert). If those or an X display are
# unavailable, fall back to the headless self-test path in references/node-graphs.md.

set -euo pipefail

BIN="${1:?usage: verify_gui_render.sh <binary> [window-title-substring] [wait-seconds]}"
TITLE="${2:-}"
WAIT="${3:-4}"
export DISPLAY="${DISPLAY:-:1}"

OUT="${OUT:-/tmp/gui_verify.png}"

for tool in xdotool import convert; do
    command -v "$tool" >/dev/null 2>&1 || { echo "missing tool: $tool (install xdotool + imagemagick)"; exit 2; }
done

if ! xdpyinfo >/dev/null 2>&1; then
    echo "no X display on DISPLAY=$DISPLAY — use the headless self-test build instead"
    exit 3
fi

echo "== launching $BIN on DISPLAY=$DISPLAY =="
"$BIN" &
APP_PID=$!
cleanup() { kill "$APP_PID" 2>/dev/null || true; }
trap cleanup EXIT

sleep "$WAIT"
if ! kill -0 "$APP_PID" 2>/dev/null; then
    echo "FAIL: app exited within ${WAIT}s (no window to capture)"
    exit 4
fi

# Find the window: by title substring if given, else the newest mapped window.
WID=""
if [[ -n "$TITLE" ]]; then
    WID="$(xdotool search --onlyvisible --name "$TITLE" 2>/dev/null | head -1 || true)"
fi
if [[ -z "$WID" ]]; then
    echo "no window matched title '$TITLE'; listing visible windows:"
    for w in $(xdotool search --onlyvisible --name "." 2>/dev/null || true); do
        printf '  %s | %s\n' "$w" "$(xdotool getwindowname "$w" 2>/dev/null || true)"
    done
    WID="$(xdotool search --onlyvisible --name "." 2>/dev/null | tail -1 || true)"
fi
[[ -n "$WID" ]] || { echo "FAIL: no visible window found"; exit 5; }

echo "== window $WID: $(xdotool getwindowname "$WID" 2>/dev/null || true) =="
import -window "$WID" "$OUT"
echo "== screenshot: $OUT =="
identify "$OUT" 2>/dev/null || true

echo "== top colours (check for YOUR palette, not desktop chrome) =="
convert "$OUT" -resize 240x -format %c histogram:info:- 2>/dev/null | sort -rn | head -12

echo
echo "Now confirm the colours your draw code emits are present above."
echo "Tip: pick 2-3 exact IM_COL32() literals from your source and grep the histogram for them."
