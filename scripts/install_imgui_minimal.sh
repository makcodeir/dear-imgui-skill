#!/usr/bin/env bash
# install_imgui_minimal.sh — download ONLY the files Dear ImGui needs, from GitHub.
#
# Usage:   bash install_imgui_minimal.sh [TAG] [DEST]
#   TAG    imgui ref to fetch: 'master', 'docking', or a tag like 'v1.92.3'   (default: master)
#   DEST   target directory (default: ./imgui)
#
# Upstream's own guidance (wiki/Getting-Started): "It is preferable that you
# build yourself from sources" — imgui is not a system library; you vendor the
# files and compile them WITH your app. This script vendors exactly those files:
#
#   core   : imgui.cpp imgui_demo.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp
#   headers: imgui.h imgui_internal.h imconfig.h imstb_rectpack.h imstb_textedit.h imstb_truetype.h
#   backend: imgui_impl_glfw.{h,cpp} + imgui_impl_opengl3.{h,cpp}  (the most common combo)
#
# Everything else in the repo (examples/, docs/, misc/, .github/) is optional.
# Want a different backend? Add its pair to BACKENDS below (e.g. imgui_impl_sdl2,
# imgui_impl_dx11, imgui_impl_vulkan) — each backend is exactly a .h/.cpp pair.
#
# Verify after:  grep -m1 '#define IMGUI_VERSION ' "$DEST/imgui.h"
# Build check:   g++ -std=c++11 -I"$DEST" -c "$DEST/imgui.cpp" -o /tmp/imgui_probe.o

set -euo pipefail

TAG="${1:-master}"
DEST="${2:-./imgui}"
BASE="https://raw.githubusercontent.com/ocornut/imgui/${TAG}"

CORE="imgui.cpp imgui_demo.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp"
HEADERS="imgui.h imgui_internal.h imconfig.h imstb_rectpack.h imstb_textedit.h imstb_truetype.h"
BACKENDS="backends/imgui_impl_glfw.h backends/imgui_impl_glfw.cpp \
          backends/imgui_impl_opengl3.h backends/imgui_impl_opengl3.cpp \
          backends/imgui_impl_opengl3_loader.h"
LICENSE="LICENSE.txt"

fetch() {
    local rel="$1" out="$DEST/${2:-$1}"
    mkdir -p "$(dirname "$out")"
    # -f: fail on HTTP 404 (bad tag/path) instead of saving the error page
    curl -fsSL "$BASE/$rel" -o "$out" || {
        echo "ERROR: fetch failed: $BASE/$rel" >&2
        echo "  -> does tag/branch '$TAG' exist? https://github.com/ocornut/imgui/tags" >&2
        exit 1
    }
}

echo "== fetching Dear ImGui ($TAG) -> $DEST =="
mkdir -p "$DEST"
for f in $CORE $HEADERS; do fetch "$f"; done
# The opengl3 loader header exists on all recent tags/branches, but guard
# against a tag where the layout differs: warn, don't hard-fail.
for f in $BACKENDS; do
    if [ "$f" = "backends/imgui_impl_opengl3_loader.h" ]; then
        fetch "$f" || echo "NOTE: $f not on this tag — opengl3 backend may still build with an external GL loader" >&2
    else
        fetch "$f"
    fi
done
fetch "$LICENSE"

# --- verify ---
VER=$(grep -m1 '#define IMGUI_VERSION ' "$DEST/imgui.h" | awk '{print $3}' | tr -d '"')
VNUM=$(grep -m1 '#define IMGUI_VERSION_NUM' "$DEST/imgui.h" | awk '{print $3}')
[ -n "$VER" ] || { echo "ERROR: imgui.h downloaded but has no IMGUI_VERSION — incomplete fetch" >&2; exit 1; }

echo "== done =="
echo "   version : $VER (IMGUI_VERSION_NUM $VNUM)"
echo "   files   : $(find "$DEST" -type f | wc -l) (5 core .cpp + 6 core headers + 5 backend incl. opengl3 loader + LICENSE)"
if [ "${VNUM:-0}" -lt 19198 ]; then
    echo "   WARNING : pre-1.92 API (old font system). 1.92+ changed fonts; see skill references/version-1.92-changes.md"
fi
cat <<EOF

   Use it (sources compile WITH your app — upstream advises against a lib):
     g++ -std=c++11 -I$DEST \\
         your_app.cpp $DEST/imgui.cpp $DEST/imgui_demo.cpp $DEST/imgui_draw.cpp \\
         $DEST/imgui_tables.cpp $DEST/imgui_widgets.cpp \\
         $DEST/backends/imgui_impl_glfw.cpp $DEST/backends/imgui_impl_opengl3.cpp \\
         \$(pkg-config --cflags --libs glfw3) -lGL -ldl -lpthread -o your_app
EOF
