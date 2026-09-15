#!/usr/bin/env bash
# build.sh — regenerate svg_icons_imgui.h from the .svg files here, then build
# BOTH the GUI demo (svg_widgets) and the headless self-test with the strict
# warning set. Run from anywhere; paths are relative to this directory.
set -e
cd "$(dirname "$0")"

# Where the imgui checkout lives (skill convention: sibling repo under Skills/imgui).
IMGUI=${IMGUI:-../../imgui}
if [ ! -f "$IMGUI/imgui.h" ]; then
    IMGUI=/home/geek/Documents/programming/Agents/Skills/imgui/imgui
fi
# GLFW: pkg-config first, else the static lib used across this machine's builds.
if pkg-config --exists glfw3 2>/dev/null; then
    GLFW_CFLAGS=$(pkg-config --cflags glfw3); GLFW_LIBS="$(pkg-config --libs glfw3)"
else
    DBSCAN=${DBSCAN:-/home/geek/Documents/programming/projects/DBSCAN}
    GLFW_CFLAGS="-I$DBSCAN/external/glfw/include"
    GLFW_LIBS="$DBSCAN/build/external/glfw/src/libglfw3.a -lX11"
fi

echo "== regenerate header from *.svg =="
python3 ../../scripts/svg2drawlist.py . -o svg_icons_imgui.h --tint FFFFFF --runtime-include

CORE="$IMGUI/imgui.cpp $IMGUI/imgui_draw.cpp $IMGUI/imgui_tables.cpp $IMGUI/imgui_widgets.cpp"
WARN="-Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Werror"

echo "== headless self-test =="
g++ -std=c++11 -I. -I"$IMGUI" -DSVG_HEADLESS_SELFTEST $WARN \
    svg_widgets.cpp $CORE -o /tmp/svg_widgets_headless -ldl -lpthread
/tmp/svg_widgets_headless

echo "== GUI app =="
g++ -std=c++11 -I. -I"$IMGUI" -I"$IMGUI/backends" $GLFW_CFLAGS $WARN \
    svg_widgets.cpp $CORE $IMGUI/backends/imgui_impl_glfw.cpp $IMGUI/backends/imgui_impl_opengl3.cpp \
    -lGL $GLFW_LIBS -ldl -lpthread -o /tmp/svg_widgets
echo "GUI binary: /tmp/svg_widgets"
