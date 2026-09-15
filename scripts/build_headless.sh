#!/usr/bin/env bash
# Strict-warning build + run gate for Dear ImGui widget/layout code.
#
# Purpose: catch the trivial mistakes (bad API, unbalanced Begin/End, wrong
# flags, missing headers) locally in one cheap command instead of burning
# tokens on compile-error round-trips. It compiles with -Wall -Wextra
# -Wpedantic -Wshadow -Wformat=2 and FAILS on the first warning.
#
# Usage:
#   bash build_headless.sh [path/to/imgui_repo]
#   IMGUI_DIR=/path/to/imgui bash build_headless.sh
#
# Defaults: IMGUI_DIR=./imgui if present, else the current directory.
# The script builds the skill's headless gallery by default; to check your own
# file instead, set SRC=/path/to/your.cpp.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILL_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
GALLERY="$SKILL_DIR/templates/gallery_headless.cpp"
SRC="${SRC:-$GALLERY}"

# Locate the Dear ImGui source tree.
if [[ -n "${IMGUI_DIR:-}" ]]; then
    IMGUI="$IMGUI_DIR"
elif [[ -f "./imgui.h" ]]; then
    IMGUI="."
elif [[ -f "./imgui/imgui.h" ]]; then
    IMGUI="./imgui"
else
    echo "ERROR: imgui.h not found. Pass the repo path:"
    echo "  bash $0 /path/to/imgui"
    echo "  IMGUI_DIR=/path/to/imgui bash $0"
    exit 2
fi

CXX="${CXX:-g++}"
OUT="${OUT:-/tmp/imgui_skill_gallery}"
WARN="-Wall -Wextra -Wpedantic -Wshadow -Wformat=2"

echo "== Dear ImGui strict build gate =="
echo "   imgui : $IMGUI"
echo "   source: $SRC"
grep -m1 '#define IMGUI_VERSION ' "$IMGUI/imgui.h" || true

# Version-era reminder.
VNUM="$(grep -m1 '#define IMGUI_VERSION_NUM' "$IMGUI/imgui.h" | tr -dc '0-9')"
if [[ -n "$VNUM" && "$VNUM" -ge 19198 ]]; then
    echo "   era   : 1.92+ dynamic fonts (no glyph ranges, PushFont needs size)"
fi

echo "-- compiling (warnings are errors) --"
"$CXX" -std=c++11 -I"$IMGUI" $WARN -Werror \
    -o "$OUT" "$SRC" \
    "$IMGUI/imgui.cpp" "$IMGUI/imgui_demo.cpp" "$IMGUI/imgui_draw.cpp" \
    "$IMGUI/imgui_tables.cpp" "$IMGUI/imgui_widgets.cpp"

echo "-- running --"
"$OUT"

echo "OK: built with zero warnings and ran clean."
