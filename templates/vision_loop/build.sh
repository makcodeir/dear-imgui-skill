#!/usr/bin/env bash
# build.sh - strict-warning build of loop_demo against the vendored imgui tree.
# Toolchain facts (this box): no system glfw3 dev pkg; static GLFW 3.5 from the
# DBSCAN build tree (see skill references/game-editor-apps.md).
set -e
cd "$(dirname "$0")"

IMGUI="${IMGUI_DIR:-}"
if [ -z "$IMGUI" ]; then
    for cand in ../imgui ../../imgui \
                /home/geek/Documents/programming/Agents/Skills/imgui/imgui; do
        if [ -f "$cand/imgui.h" ] && [ -d "$cand/backends" ]; then IMGUI="$cand"; break; fi
    done
fi
[ -n "$IMGUI" ] || { echo "no imgui checkout found; set IMGUI_DIR=/path/to/imgui"; exit 1; }
GLFW_ROOT=${GLFW_ROOT:-/home/geek/Documents/programming/projects/DBSCAN}

g++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Werror \
    -I"$IMGUI" -I"$IMGUI/backends" -I"$GLFW_ROOT/external/glfw/include" \
    loop_demo.cpp \
    "$IMGUI"/imgui.cpp "$IMGUI"/imgui_demo.cpp "$IMGUI"/imgui_draw.cpp \
    "$IMGUI"/imgui_tables.cpp "$IMGUI"/imgui_widgets.cpp \
    "$IMGUI"/backends/imgui_impl_glfw.cpp "$IMGUI"/backends/imgui_impl_opengl3.cpp \
    "$GLFW_ROOT/build/external/glfw/src/libglfw3.a" \
    -lGL -lX11 -ldl -lpthread \
    -o /tmp/loop_demo
echo "built: /tmp/loop_demo"
