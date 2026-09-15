#!/usr/bin/env bash
# build.sh — dreamIDE ("Project Desk"): strict build with zero warnings.
# Toolchain discovered on this box (no system GLFW dev package):
#   GLFW 3.5 static lib + headers from the DBSCAN build tree.
set -e
cd "$(dirname "$0")"

GLFW_ROOT=/home/geek/Documents/programming/projects/DBSCAN
IMGUI=../imgui

g++ -std=c++20 -O2 -I. -I$IMGUI -I$IMGUI/backends -I$GLFW_ROOT/external/glfw/include \
    -Wall -Wextra -Wpedantic -Wshadow \
    main.cpp App.cpp Panels.cpp \
    $IMGUI/imgui.cpp $IMGUI/imgui_demo.cpp $IMGUI/imgui_draw.cpp \
    $IMGUI/imgui_tables.cpp $IMGUI/imgui_widgets.cpp \
    $IMGUI/misc/cpp/imgui_stdlib.cpp \
    $IMGUI/backends/imgui_impl_glfw.cpp $IMGUI/backends/imgui_impl_opengl3.cpp \
    $GLFW_ROOT/build/external/glfw/src/libglfw3.a \
    -lGL -lX11 -ldl -lpthread \
    -o /tmp/dreamide
echo "Binary: /tmp/dreamide"
