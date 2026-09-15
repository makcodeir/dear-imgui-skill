#!/usr/bin/env bash
# build.sh — build both binaries (GUI app + headless self-test) with strict warnings.
# Toolchain discovered on this box (no system GLFW dev package):
#   GLFW 3.5 static lib + headers from the DBSCAN build tree (see skill cmake-integration.md)
set -e
cd "$(dirname "$0")"

GLFW_ROOT=/home/geek/Documents/programming/projects/DBSCAN
IMGUI=../imgui

SRC_COMMON="platformer_engine.cpp platformer_aiclient.cpp"

echo "== headless self-test =="
g++ -std=c++20 -O2 -I. -Wall -Wextra -Wpedantic -Wshadow \
    platformer_selftest.cpp $SRC_COMMON -lssl -lcrypto -o /tmp/platformer_selftest
/tmp/platformer_selftest

echo "== GUI app =="
g++ -std=c++20 -O2 -I. -I$IMGUI -I$IMGUI/backends -I$GLFW_ROOT/external/glfw/include \
    -Wall -Wextra -Wpedantic -Wshadow \
    main.cpp platformer_render.cpp $SRC_COMMON \
    $IMGUI/imgui.cpp $IMGUI/imgui_demo.cpp $IMGUI/imgui_draw.cpp \
    $IMGUI/imgui_tables.cpp $IMGUI/imgui_widgets.cpp \
    $IMGUI/backends/imgui_impl_glfw.cpp $IMGUI/backends/imgui_impl_opengl3.cpp \
    $GLFW_ROOT/build/external/glfw/src/libglfw3.a \
    -lGL -lX11 -lssl -lcrypto -ldl -lpthread \
    -o /tmp/platformer
echo "GUI binary: /tmp/platformer"
