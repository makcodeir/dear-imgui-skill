# CMake integration + toolchain discovery for ImGui apps

## Discovering the machine's windowing stack (do this BEFORE writing CMakeLists)

Never assume a dev package exists. Probe in order:

```bash
pkg-config --modversion glfw3 || echo "no glfw3"
pkg-config --modversion sdl2  || echo "no sdl2"
ldconfig -p | grep -E 'libglfw|libSDL2'          # runtime libs only?
apt list --installed 2>/dev/null | grep -Ei 'glfw|libsdl'
# look for PREVIOUSLY-BUILT static libs on the machine — often the only usable GLFW:
find /home/<user> -name "libglfw3.a" 2>/dev/null
find /home/<user> -path "*glfw*" -name "glfw3.h" 2>/dev/null
```

A `libglfw3.a` inside another project's build tree (e.g. `<project>/build/external/glfw/src/`)
pairs with headers at that project's `external/glfw/include/`. Link it directly — no install,
no sudo, works immediately. Verify the whole stack with a ~40-line smoke program
(glfwInit → CreateContext → 5 frames of NewFrame/Render) before writing any real code;
if it exits 0, every path in your CMakeLists is proven.

## Working CMakeLists.txt (static local GLFW + imgui sources)

```cmake
cmake_minimum_required(VERSION 3.16)
project(dodo)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(GLFW_DIR /path/to/external/glfw)                                # headers: ${GLFW_DIR}/include
set(GLFW_LIB /path/to/build/external/glfw/src/libglfw3.a)
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../imgui/imgui)           # NOTE: checkout root may be nested

add_executable(dodo
    main.cpp
    <your .cpp files>
    ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(dodo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${IMGUI_DIR} ${IMGUI_DIR}/backends ${GLFW_DIR}/include)
target_link_libraries(dodo PRIVATE ${GLFW_LIB} GL dl pthread)
```

## Pitfalls (each cost a real configure/compile cycle)

1. **Relative source paths don't resolve.** `add_executable(app ../imgui/imgui.cpp)` →
   `Cannot find source file: ../imgui/imgui.cpp` then a misleading `No SOURCES given to target`.
   CMake resolves source paths from the **build** dir. Always use
   `${CMAKE_CURRENT_SOURCE_DIR}/...` for out-of-tree sources.
2. **Nested checkout roots.** Verify with `ls <root>/imgui.h <root>/backends/` — a
   `Skills/imgui/imgui/` layout means the sources are one level deeper than the folder name suggests.
3. **`rm -rf build` between failed configures.** CMake can cache the failed dependency scan;
   a fresh cache removes one whole class of phantom errors.
4. **First-pass compile fixes to expect** when moving from smoke test to real app:
   - `ImGui::IsKeyPressed(GLFW_KEY_X)` → use `ImGuiKey_X` (int→ImGuiKey is a hard error on 1.92+).
   - `ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical)` is internal (`imgui_internal.h`);
     use `ImGui::Text("|")` between `SameLine()`s instead.
   - `ImDrawList` method is `AddPolyline(pts, n, col, ImDrawFlags_Closed, thickness)` — no `Polyline`.
   - Custom `Vec2`/`Color` structs don't implicitly convert to `ImVec2`/`ImU32`; make draw helpers
     take/return `ImVec2` and convert with `ColorConvertFloat4ToU32` explicitly.
   - Readers that take `const World&` need a `const T* Find(...) const` overload on the container.
