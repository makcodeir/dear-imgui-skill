#!/usr/bin/env bash
# verify_game_motion.sh - MODEL-FREE pass/fail gate for games & simulations.
#
# Why this exists: capture_for_vision.sh and capture_frames_grid.sh are one-way
# (they produce images for a vision model to read). For games/sims you also want
# a cheap, deterministic, non-LLM gate that fails with exit 1 - and a contact
# sheet of small entities is known to read as "frozen" even when the sim is
# running. This script produces the numeric truth instead.
#
# What it asserts (each line prints PASS/FAIL; exit 1 if any FAIL):
#   1. the window maps within the launch timeout
#   2. settled: two consecutive frames become bit-identical (waits out the
#      initial-paint/focus repaint; reported as INFO, it is not a gate)
#   3. idle control: two frames with NO input between them are bit-identical
#      (RMSE exactly 0) -> the idle/attract state does not drift. This is the
#      control for check 4, not decoration: it proves the capture path and the
#      app state are stable enough for a diff to mean anything.
#   4. motion: two frames AFTER the action key DIFFER (RMSE > 0) -> something MOVED
#   5. localized: the changed-pixel bounding box is smaller than MAX_CHANGE_PCT
#      of the window -> motion is LOCALIZED, not a global repaint/flicker
#   6. every colour in EXPECT_COLORS is painted in >=1 running frame -> the
#      entity/player is really drawn
#   7. the quit key exits the process with status 0 -> clean shutdown
#
# Scope note (deliberate; do not advertise what is not implemented): this does
# NOT check layout, clipping, or letterbox/stray-pixel hygiene. Those need
# per-app knowledge of which colour is canvas vs letterbox plus a full-pixel
# sweep; keep them in the app's own test harness (see "A letterboxed canvas
# paints outside itself" in SKILL.md) and use capture_for_vision.sh for the
# layout read.
#
# Usage:
#   bash verify_game_motion.sh <binary> <window-title-substring>
#
# The title argument is REQUIRED and must match exactly one window. There is
# deliberately no "pick any window" fallback: an unmatched title that silently
# latches onto another window (a terminal, a sibling app) makes the script
# verify the WRONG PIXELS and report a confident verdict about them. Set
# ALLOW_ANY_WINDOW=1 only when you accept that risk - the chosen window's name
# and geometry are always echoed.
#
# Env knobs (all optional):
#   ACT_KEY="space"        key sent to start/advance the sim (xdotool key name)
#   QUIT_KEY="Escape"      key that must quit the app cleanly
#   IDLE_S=0.45            gap between the two idle frames
#   RUN_S=1.0              gap between running frames
#   SETTLE_S=0.3           gap between settle probes
#   SETTLE_TRIES=12        max settle probes
#   EXPECT_COLORS="74,124,89 83,83,83"   space-separated r,g,b triples that must
#                          appear in at least one running frame
#   COLOR_FUZZ=2           tolerance, percent, when counting a colour
#   MAX_CHANGE_PCT=90      max %% of the window allowed to change between run frames
#   SHOT_DIR=/tmp/game_motion
#   WAIT_S=20              seconds to wait for the window to map
#   KEEP_ALIVE=1           leave the app running at the end (default: kill it)
#
# Requires: xdotool + imagemagick (import/compare/convert/identify) + an X display.

set -uo pipefail

BIN="${1:?usage: verify_game_motion.sh <binary> <window-title-substring>}"
TITLE="${2:?usage: verify_game_motion.sh <binary> <window-title-substring>}"
export DISPLAY="${DISPLAY:-:1}"

ACT_KEY="${ACT_KEY:-space}"
QUIT_KEY="${QUIT_KEY:-Escape}"
IDLE_S="${IDLE_S:-0.45}"
RUN_S="${RUN_S:-1.0}"
SETTLE_S="${SETTLE_S:-0.3}"
SETTLE_TRIES="${SETTLE_TRIES:-12}"
EXPECT_COLORS="${EXPECT_COLORS:-}"
COLOR_FUZZ="${COLOR_FUZZ:-2}"
MAX_CHANGE_PCT="${MAX_CHANGE_PCT:-90}"
SHOT_DIR="${SHOT_DIR:-/tmp/game_motion}"
WAIT_S="${WAIT_S:-20}"
KEEP_ALIVE="${KEEP_ALIVE:-0}"
ALLOW_ANY_WINDOW="${ALLOW_ANY_WINDOW:-0}"

for tool in xdotool import compare convert identify; do
    command -v "$tool" >/dev/null 2>&1 || { echo "missing tool: $tool"; exit 2; }
done
xdpyinfo >/dev/null 2>&1 || { echo "no X display on DISPLAY=$DISPLAY"; exit 3; }
[ -x "$BIN" ] || [ -f "$BIN" ] || { echo "not runnable: $BIN"; exit 2; }

NAME="$(basename "$BIN" | tr ' .' '__')"
mkdir -p "$SHOT_DIR"
rm -f "$SHOT_DIR/$NAME"_*.png "$SHOT_DIR/$NAME.stderr"
LOG="$SHOT_DIR/$NAME.stderr"

FAILS=0
check() { # check <label> <ok:0|1> [detail]
    if [ "$2" = "0" ]; then
        echo "  PASS  $1${3:+  [$3]}"
    else
        echo "  FAIL  $1${3:+  [$3]}"
        FAILS=$((FAILS + 1))
    fi
}
info() { echo "  INFO  $1${2:+  [$2]}"; }

# RMSE between two images. imagemagick prints "<raw> (<normalized>)", e.g.
# "0 (0)" for identical frames. compare exits 1 when images differ - never let
# that escape, and take the first field (0 iff bit-identical).
rmse() {
    compare -metric RMSE "$1" "$2" null: 2>&1 | head -1 | awk '{print ($1=="" ? "nan" : $1)}'
}

# Count pixels matching one r,g,b triple (fuzz-tolerant). Bilevel the image,
# then mean*w*h is the count of the white pixels.
count_color() { # count_color <file> <r,g,b>
    convert "$1" -fuzz "${COLOR_FUZZ}%" -fill black +opaque "rgb($2)" \
        -fill white -opaque "rgb($2)" -format '%[fx:int(mean*w*h+0.5)]' info: 2>/dev/null || echo 0
}

echo "== launching $BIN =="
pkill -xf "$BIN" 2>/dev/null || true   # determinism guard: no stale instance of the same binary
sleep 0.4
"$BIN" >/dev/null 2>"$LOG" &
APP_PID=$!
cleanup() { if [ "$KEEP_ALIVE" != "1" ]; then kill "$APP_PID" 2>/dev/null || true; fi; }
trap cleanup EXIT

# --- find OUR window: title match only, retried until the deadline ---------
WID=""
DEADLINE=$(( $(date +%s) + WAIT_S ))
while [ "$(date +%s)" -lt "$DEADLINE" ]; do
    WID="$(xdotool search --onlyvisible --name "$TITLE" 2>/dev/null | head -1 || true)"
    [ -n "$WID" ] && break
    kill -0 "$APP_PID" 2>/dev/null || break
    sleep 0.25
done

if [ -z "$WID" ]; then
    if [ "$ALLOW_ANY_WINDOW" = "1" ]; then
        WID="$(xdotool search --onlyvisible --name "." 2>/dev/null | tail -1 || true)"
        info "no window matched '$TITLE' - ALLOW_ANY_WINDOW=1 fallback in use" "verify this is really your app"
    fi
fi
if [ -z "$WID" ]; then
    echo "  FAIL  window matching '$TITLE' mapped within ${WAIT_S}s"
    echo "  (the app may be alive but untitled, or it exited - see the stderr tail below)"
    echo "== app stderr (if any) =="
    tail -20 "$LOG" 2>/dev/null
    exit 4
fi

WNAME="$(xdotool getwindowname "$WID" 2>/dev/null || echo "?")"
WGEOM="$(xdotool getwindowgeometry --shell "$WID" 2>/dev/null | tr '\n' ' ' || echo "?")"
echo "  PASS  window mapped within ${WAIT_S}s  [id=$WID name='$WNAME' $WGEOM]"

# focus truth: if another window holds focus, the action key goes THERE and the
# motion gate fails for a reason that has nothing to do with the game.
timeout 4 xdotool windowactivate "$WID" >/dev/null 2>&1 || echo "  (windowactivate timed out - continuing)"
sleep 0.4
ACTIVE="$(xdotool getactivewindow 2>/dev/null || echo "")"
if [ -n "$ACTIVE" ] && [ "$ACTIVE" != "$WID" ]; then
    info "focus is on window $ACTIVE, not $WID - key injection may be misdirected"
fi

shot() { import -window "$WID" "$SHOT_DIR/${NAME}_$1.png" 2>/dev/null; }
size_of() { identify -format '%wx%h' "$1" 2>/dev/null || echo "?x?"; }

echo
echo "-- 0. settle (wait out the initial paint/focus repaint) --"
shot settle_a
SETTLED="no"
for i in $(seq 1 "$SETTLE_TRIES"); do
    sleep "$SETTLE_S"
    shot settle_b
    if [ "$(rmse "$SHOT_DIR/${NAME}_settle_a.png" "$SHOT_DIR/${NAME}_settle_b.png")" = "0" ]; then
        SETTLED="yes"
        info "window settled after $i probe(s)"
        break
    fi
    mv "$SHOT_DIR/${NAME}_settle_b.png" "$SHOT_DIR/${NAME}_settle_a.png"
done
[ "$SETTLED" = "yes" ] || info "window never went bit-identical in $SETTLE_TRIES probes - it may animate continuously" "idle gate below may legitimately fail"

echo
echo "-- 1. idle control (frames before any input) --"
shot idle1
sleep "$IDLE_S"
shot idle2
IDLE_RMSE="$(rmse "$SHOT_DIR/${NAME}_idle1.png" "$SHOT_DIR/${NAME}_idle2.png")"
check "idle frames bit-identical over ${IDLE_S}s (no drift before input)" \
      "$(awk -v v="$IDLE_RMSE" 'BEGIN{print (v=="0")?0:1}')" "rmse=$IDLE_RMSE"

echo
echo "-- 2. motion after action key '$ACT_KEY' --"
xdotool key --clearmodifiers "$ACT_KEY"
sleep 0.5
shot run1
sleep "$RUN_S"
shot run2
sleep "$RUN_S"
shot run3
kill -0 "$APP_PID" 2>/dev/null
check "app still running after the action key" "$?" "pid=$APP_PID"

RUN_RMSE="$(rmse "$SHOT_DIR/${NAME}_run1.png" "$SHOT_DIR/${NAME}_run2.png")"
check "running frames DIFFER (something is moving)" \
      "$(awk -v v="$RUN_RMSE" 'BEGIN{print (v+0>0)?0:1}')" "rmse=$RUN_RMSE"

# Localization: bounding box of the pixels that actually changed. Do NOT trim
# the image compare writes into the 3rd argument - for EQUAL pixels ImageMagick
# paints a faded copy of the reference frame, so trimming that file measures the
# CANVAS bbox and reports a confident, meaningless percentage (measured here:
# 51.1% on two frames whose RMSE was exactly 0). A difference composite turns
# equal pixels black, so threshold+trim really is the bbox of the change; on
# identical frames it collapses to 1x1 (= 0.0%), which is the honest answer.
CHANGE_BOX_FILE="$SHOT_DIR/${NAME}_diff.png"
convert "$SHOT_DIR/${NAME}_run1.png" "$SHOT_DIR/${NAME}_run2.png" \
        -compose difference -composite "$CHANGE_BOX_FILE" 2>/dev/null || true
CHANGE_BOX="$(convert "$CHANGE_BOX_FILE" -colorspace gray -threshold 5% \
              -trim +repage -format '%wx%h' info: 2>/dev/null || echo "")"
WINSZ="$(size_of "$SHOT_DIR/${NAME}_run1.png")"
CHANGE_PCT="$(awk -v box="$CHANGE_BOX" -v win="$WINSZ" 'BEGIN{
    split(box,b,"x"); split(win,w,"x");
    if (b[1]=="" || w[1]=="" || w[1]==0) { print -1; exit }
    printf "%.1f", 100*(b[1]*b[2])/(w[1]*w[2])
}')"
if [ "$CHANGE_PCT" = "-1" ]; then
    check "changed-pixel bbox is localized (<${MAX_CHANGE_PCT}% of window)" 1 "could not measure bbox"
else
    check "changed-pixel bbox is localized (<${MAX_CHANGE_PCT}% of window)" \
          "$(awk -v p="$CHANGE_PCT" -v m="$MAX_CHANGE_PCT" 'BEGIN{print (p<m)?0:1}')" \
          "changed=${CHANGE_BOX} of ${WINSZ} = ${CHANGE_PCT}%"
fi

if [ -n "$EXPECT_COLORS" ]; then
    echo
    echo "-- 3. expected colours are actually painted --"
    for triple in $EXPECT_COLORS; do
        BEST=0
        BESTF=""
        for f in run1 run2 run3; do
            n="$(count_color "$SHOT_DIR/${NAME}_$f.png" "$triple")"
            [ "${n:-0}" -gt "${BEST:-0}" ] && { BEST="$n"; BESTF="$f"; }
        done
        check "colour rgb($triple) present in a running frame" \
              "$(awk -v n="${BEST:-0}" 'BEGIN{print (n>=1)?0:1}')" \
              "max=${BEST}px in ${BESTF:-none}"
    done
fi

echo
echo "-- 4. clean shutdown on '$QUIT_KEY' --"
xdotool key --clearmodifiers "$QUIT_KEY"
for _ in $(seq 1 32); do kill -0 "$APP_PID" 2>/dev/null || break; sleep 0.25; done
if kill -0 "$APP_PID" 2>/dev/null; then
    check "'$QUIT_KEY' quits the app" 1 "still alive after 8s"
else
    wait "$APP_PID" 2>/dev/null
    CODE=$?
    check "'$QUIT_KEY' exits with status 0" "$(awk -v c="$CODE" 'BEGIN{print (c==0)?0:1}')" "exit=$CODE"
fi

echo
echo "==============================================================="
echo "shots:  $SHOT_DIR/${NAME}_*.png"
echo "stderr: $LOG"
[ -s "$LOG" ] && { echo "-- app stderr (tail) --"; tail -10 "$LOG"; }
if [ "$FAILS" = "0" ]; then echo "ALL MOTION CHECKS PASSED"; else echo "$FAILS CHECK(S) FAILED"; fi
echo "next: bash capture_frames_grid.sh $BIN \"$TITLE\" 4 2   # vision read of the motion"
echo "==============================================================="
[ "$FAILS" = "0" ] && exit 0 || exit 1
