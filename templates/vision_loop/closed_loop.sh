#!/usr/bin/env bash
# closed_loop.sh - the automated visual-verification loop in one script:
# launch -> readiness via control socket (no blind sleep) -> action sequence ->
# settle via sync -> per-step screenshot -> manifest of (state line, image path).
#
# Usage: bash closed_loop.sh [binary] [window-title]     (defaults below)
# The manifest is what an agent feeds to its vision tool: for each STEP, pass
# shot=... to vision_analyze with a question naming the expected state shown
# on the state=... line. Text assertion and pixels must agree.

set -uo pipefail
cd "$(dirname "$0")"

BIN="${1:-/tmp/loop_demo}"
TITLE="${2:-Vision Loop Demo}"
SOCK="/tmp/loop_demo_$$.sock"
OUTDIR=/tmp/imgui_vision
NAME="$(basename "$BIN" | tr ' .' '__')"

export DISPLAY="${DISPLAY:-:1}"
for tool in xdotool import convert python3; do
    command -v "$tool" >/dev/null 2>&1 || { echo "missing tool: $tool"; exit 2; }
done
xdpyinfo >/dev/null 2>&1 || { echo "no X display on $DISPLAY"; exit 3; }
mkdir -p "$OUTDIR"

# Determinism guard (skill Pitfall 27): kill stale same-binary instances first.
# -x exact-match avoids the self-kill trap of -f matching this script's argv.
pkill -xf "$BIN" 2>/dev/null || true
sleep 0.3

"$BIN" --control "$SOCK" &
APP_PID=$!
cleanup() { kill "$APP_PID" 2>/dev/null || true; rm -f "$SOCK"; }
trap cleanup EXIT

d() { python3 drive.py "$SOCK" "$@"; }   # one control-API command

# step_capture <label> <shot-suffix>: settle, capture window, print manifest line
step_capture() {
    local label="$1" suffix="$2"
    d sync $(( $(d frame | sed 's/.*frame=//') + 2 )) >/dev/null
    local WID STATE FULL SMALL
    WID="$(xdotool search --onlyvisible --name "$TITLE" | head -1)"
    [[ -n "$WID" ]] || { echo "STEP $label ERR no window"; return 1; }
    xdotool windowactivate --sync "$WID" 2>/dev/null || true
    FULL="$OUTDIR/${NAME}_${suffix}_full.png"; SMALL="$OUTDIR/${NAME}_${suffix}_small.png"
    import -window "$WID" "$FULL" && convert "$FULL" -resize 1024x "$SMALL"
    STATE="$(d state)"
    echo "STEP $label  $STATE  shot=$SMALL"
}

echo "== waiting for control socket (launch readiness gate) =="
d wait_ready 10 || { echo "FAIL: app not ready on $SOCK"; exit 4; }

echo "== manifest =="
step_capture baseline s1 || exit 5

# 1) three REAL clicks on the + button (synthetic mouse -> live rect -> hit-test)
for i in 1 2 3; do
    QT=$(d press inc | sed 's/.*queued_through=//')
    d sync $((QT + 2)) >/dev/null
done
step_capture "press inc x3" s2 || exit 5

# 2) direct model write (state setup path)
AP=$(d set gauge 0.95 | sed 's/.*applied_frame=//')
d sync $((AP + 2)) >/dev/null
step_capture "set gauge 0.95" s3 || exit 5

# 3) checkbox click -> canvas colour must change (verifiable in pixels)
QT=$(d press blue | sed 's/.*queued_through=//')
d sync $((QT + 2)) >/dev/null
step_capture "press blue" s4 || exit 5

# 4) negative control: bad names must error loudly, never silently
d press nosuch
d set nosuch 1

d quit >/dev/null
wait "$APP_PID"; RC=$?
echo "== app exit code: $RC =="
echo "== pass criterion: every STEP state= line matches its action AND the vision"
echo "   read of shot= agrees (Count text / blue canvas / gauge bar width). =="
