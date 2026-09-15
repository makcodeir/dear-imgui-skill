# Game/Editor-Style Canvas Apps (2D game engine case study)

Findings from building a complete 2D game engine + editor (world objects,
camera, transform gizmo, hierarchy/inspector panels, play mode, scene
save/load) against a bare Linux box with no GLFW/SDL dev packages.

## Workflow: verify the toolchain BEFORE writing the plan

The machine had no `libglfw3-dev`/`libsdl2-dev`, but a usable static GLFW
existed inside an unrelated project's build tree. Discovery order that worked:

1. `ldconfig -p | grep -E 'libglfw|libSDL2'` — runtime-only libs (no headers).
2. Search for local static builds: `find ~ -name "libglfw3.a"` — found one in
   `~/projects/<other>/build/external/glfw/src/`, headers in
   `~/projects/<other>/external/glfw/include/`.
3. Confirm GL dev symlinks exist: `/usr/lib/x86_64-linux-gnu/libGL.so`.
4. Write a ~30-line smoke test (GLFW init + ImGui lifecycle + 5 frames),
   compile it with the exact static-lib link, run on the live DISPLAY, check
   exit 0. Only then plan/commit to the stack.

Link line for static GLFW (no pkg-config needed):
`libglfw3.a -lGL -ldl -lpthread`. On Linux, `#define GLFW_DLL` before
including `GLFW/glfw3.h` is a no-op (dllimport is Windows-only); harmless with
a static lib.

## CMake: source paths resolve against the BUILD dir

`add_executable(... ../imgui/imgui.cpp)` fails with "Cannot find source file"
— relative *source* paths are taken from the build directory, while
`target_include_directories` relatives resolve from the CMakeLists dir. Always
use `${CMAKE_CURRENT_SOURCE_DIR}/../imgui/...` for sources. Also verify actual
checkout nesting: this repo root was `imgui/` with sources one level down in
`imgui/imgui/`.

## Three-panel editor layout (Hierarchy | canvas | Inspector)

- NEVER give the middle canvas `ImVec2(-FLT_MIN, 0)` (fill width) inside a
  SameLine row: the fill-width child consumes ALL remaining width and pushes
  the right-hand panel completely off-window. It still "renders" — just where
  nobody can see it. Compile + exit 0 will not catch this; a screenshot will.
- Split widths explicitly: canvas width = `GetContentRegionAvail().x` minus
  the fixed side panel widths (minus SameLine spacing).
- Give all three children the same computed row height, reserving one line for
  a status bar:
  `float row_h = GetContentRegionAvail().y - GetFrameHeightWithSpacing();`
- Auto-select a demo object at editor init so the inspector renders populated
  on first run — a "no object selected" panel hides broken binding bugs.

## GLFW key polling: probe the valid range

Blindly polling `glfwGetKey(w, k)` for k in 0..511 makes older GLFW builds
emit `GLFW error 65539: Invalid key` for every rejected code — thousands of
lines per run. Probe what the linked build accepts:

```c
for (int k = 0; k <= 512; k++) {
    glfwGetKey(w, k);
    if (glfwGetError(NULL) == GLFW_NO_ERROR) printf(" %d", k);
}
```

One real-world build accepted exactly [32, 348]. Nothing usable is lost by
polling `[32, GLFW_KEY_LAST]`: GLFW uses nothing below 32 (SPACE=32, A-Z=65-90,
arrows=262-265, ESC=256, F-keys=290+). Cache KEY_MIN=32 / KEY_MAX=GLFW_KEY_LAST
in your input struct and guard `Down()`/`Pressed()` with it.

## ImGui key API vs platform codes

- `ImGui::IsKeyPressed(GLFW_KEY_SPACE, false)` is a compile error on 1.92+
  (`int` → `ImGuiKey` conversion). Use `ImGuiKey_Space`. Guardrails: engine
  code may legitimately use raw GLFW codes (it owns the window); ImGui calls
  must translate to ImGuiKey_*.
- `SeparatorEx` / `ImGuiSeparatorFlags` are in `imgui_internal.h` — not public
  API. For a vertical divider in a toolbar, `ImGui::Text("|")` works; or use
  public `ImGui::Separator()`.

## Headless logic tests for engine code

Game-logic layers (world, snapshot/play-state, save/load) are testable without
a window or GL: compile the engine .cpps plus a plain `main()` test. The input
layer references `glfwGetKey`, so link the static libglfw3.a + `-ldl
-lpthread` — it runs fine with no display and no OpenGL. Cover: spawn/destroy,
save→load roundtrip (all fields), play integration (`pos += vel*dt`), stop
restores snapshot but preserves edits, wrap-around bounds, corrupt-file
rejection with a message. Pair with `verify_gui_render.sh` for the pixel side:
two independent signals (logic tests + screenshot) beat either alone.
