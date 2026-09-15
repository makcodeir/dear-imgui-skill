---
name: imgui-cpp-guis
description: "Use when writing, reviewing, or debugging Dear ImGui C++ GUI code (windows, widgets, layout, tables, menus, modals, fonts). Targets the 1.92+/1.93 API, gives exact signatures and flags so you stop guessing at the API, and removes the trivial mistakes (Begin/End pairing, ID collisions, obsolete font calls, wrong lifecycle order) that waste tokens and compile cycles."
version: 1.8.1
author: Hermes Agent
license: MIT
platforms: [linux, macos, windows]
metadata:
  hermes:
    tags: [imgui, gui, cpp, immediate-mode, widgets, layout, vision-qa, closed-loop-qa, svg]
    related_skills: [hermes-agent-skill-authoring, plan]
---

# Building GUIs with Dear ImGui (C++)

## Overview

Dear ImGui is an immediate-mode GUI library: **you re-emit the entire UI every
frame**. There is no widget tree to keep in sync, no callbacks, no retained
objects. Bytes go into a vertex buffer; a backend you provide draws them.

This skill is deliberately API-precise. The single biggest token sink when
working with Dear ImGui is guessing at a function signature or a flag name and
being wrong, then re-reading `imgui.h` (435 KB) to recover. The tables here
exist so you do not have to. When you need the full parameter list, `read_file`
the specific declaration — never re-derive it from memory.

**Version this skill targets:** Dear ImGui **1.93.0 WIP** (`IMGUI_VERSION_NUM
19297`, `IMGUI_HAS_TEXTURES`). This is the *dynamically-scaled font* era
(1.92, June 2025). Much content online is pre-1.92 and **now actively wrong** —
see `references/version-1.92-changes.md` before copying any tutorial snippet.

## When to Use

- Writing new ImGui windows, widgets, layout, tables, menus, or modals in C++.
- Reviewing or fixing ImGui code that misbehaves (blank window, assert, ID
  collision, wrong size, stray label).
- Integrating ImGui into an app (backend init / NewFrame / Render / shutdown).
- Loading fonts, styling, or handling DPI.
- Building **node graph / dataflow editors** — flowcharts, visual scripting
  (UE Blueprints), digital logic circuit simulators, and automation graphs.
  See `references/node-graphs.md`, `references/logic-circuit-simulator.md`,
  `templates/node_graph_editor.cpp`, and `templates/LogicCircuitSimulator/`.
- Custom drawing with `ImDrawList` (beziers, filled shapes, grid backgrounds).
- Building **full-window editor / game-tool apps** (canvas + panels, custom
  rendering, scene data, play mode) — see `references/game-editor-apps.md` and
  `templates/game_editor_template.h`.
- Building **IDE-style workspace apps** (menu bar + panel shell, file explorer
  tree, tabbed text editor, output console, Kanban board, command palette,
  shortcut chords) — see `references/ide-workspace-apps.md` and
  `templates/DreamIDE/`.
- Verifying a GUI **as an agent** (no eyeballs): histogram gate + screenshot
  for a vision model — see "The Vision Loop" and `scripts/capture_for_vision.sh`.
- Running a **closed verification loop** (act → settle → capture → assert):
  embed the UDS control harness, drive real widget clicks via injected `io`
  events, sync on the frame counter — see
  `references/automated-visual-verification.md` and `templates/vision_loop/`.
- **SVG for GUIs**: authoring GUI-oriented SVGs (icons, spinners, loaders) and
  converting them to live ImDrawList C++ (SMIL animations included) — see
  `references/svg-for-imgui.md`, `scripts/svg2drawlist.py`, `templates/svg/`.

Don't use for: ImVec math beyond the basics (see `api-quickref.md`), ImPlot /
ImPlot3d (separate libraries), or writing a custom renderer from scratch.

## The One Thing That Matters Most: API Version

Before writing a single line, know which ImGui version is in the tree:

```bash
grep -m1 '#define IMGUI_VERSION ' imgui.h     # human-readable
grep -m1 '#define IMGUI_VERSION_NUM' imgui.h  # numeric, for #if guards
```

If `IMGUI_VERSION_NUM >= 19198` (1.92+) you are on the dynamic font system.
That changes font loading, `PushFont`, and glyph ranges. **Do not** write
pre-1.92 font code against a 1.92+ tree; it either fails to compile or asserts.

## Getting the Library (install from source)

ImGui is **not a system library** — you vendor ~17 files and compile them with
your app (upstream: *"preferable that you build yourself from sources"*; do not
build a static/shared lib, do not `apt install libimgui-dev` — distro packages
ship the pre-1.92 font API). Three routes, fastest first:

```bash
# A. Minimal files from GitHub raw — 17 files (incl. imgui_impl_opengl3_loader.h, required on 1.93+), verified complete:
bash scripts/install_imgui_minimal.sh master third_party/imgui

# B. Full clone (adds examples/, docs/, misc/ — 131 MB with .git):
git clone --depth 1 https://github.com/ocornut/imgui.git third_party/imgui

# C. Pinned tarball:
curl -LO https://github.com/ocornut/imgui/archive/refs/tags/v1.92.3.tar.gz
```

Then compile the 5 core `.cpp` + your backend pair (e.g.
`backends/imgui_impl_glfw.cpp` + `imgui_impl_opengl3.cpp`) into your target —
the exact command is at the top of this file and printed by the script.
Full file list, backend-swap table, verification probes and traps:
`references/installing.md`.

## The Frame Lifecycle (get the order right)

Every example in `examples/` follows this exactly. Getting it wrong is the #1
cause of asserts and blank windows.

```cpp
// --- init ---
IMGUI_CHECKVERSION();            // asserts if headers/impl mismatch
ImGui::CreateContext();
ImGuiIO& io = ImGui::GetIO();    // (void)io; if you use no io fields yet
ImGui::StyleColorsDark();
ImGui_ImplGlfw_InitForOpenGL(window, true);   // platform backend
ImGui_ImplOpenGL3_Init(glsl_version);         // renderer backend

// --- per frame ---
ImGui_ImplOpenGL3_NewFrame();    // renderer backend FIRST
ImGui_ImplGlfw_NewFrame();       // platform backend SECOND
ImGui::NewFrame();               // then NewFrame
// ... emit your UI here ...
ImGui::Render();
ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
// ... swap buffers ...

// --- shutdown (reverse of init) ---
ImGui_ImplOpenGL3_Shutdown();
ImGui_ImplGlfw_Shutdown();
ImGui::DestroyContext();
```

Rules that are not obvious:

- **Renderer `_NewFrame()` before platform `_NewFrame()` before `NewFrame()`.**
- `IMGUI_CHECKVERSION()` goes *before* `CreateContext()`.
- Shutdown in reverse order of init; `DestroyContext()` last of the ImGui calls.
- `Begin()` / `End()` and `BeginChild()` / `EndChild()` **always pair, even when
  the Begin returns false** (these two are documented exceptions:
  *"Always call a matching EndChild() for each BeginChild() call, regardless of
  its return value"*). Every other `Begin*` (popup/table/combo/listbox/tabbar/
  tabitem/menubar/menu/tooltip/mainmenubar) needs its `End*` **only when it
  returned true**, and `TreeNode` needs `TreePop()` only when true.
  `BeginGroup`/`BeginDisabled`/`BeginMultiSelect` return void/pointer and always
  pair. See the pairing matrix in `references/api-quickref.md`.
- A hand-rolled/headless loop must set `io.DeltaTime` and `io.DisplaySize`
  itself (the platform backend's `_NewFrame()` normally does this) or
  `NewFrame()` asserts on an invalid DisplaySize.
- A headless smoke test with **no renderer backend** must also set
  `io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures`, or `NewFrame()`
  asserts that the font atlas was never built/uploaded (in `imgui_draw.cpp`,
  `ImFontAtlasUpdateNewFrame`). Real apps get this flag from
  `ImGui_ImplXxx_Init()`. `templates/gallery_headless.cpp` sets all three.

- **Headless "pretend to present" loop (recommended).** Setting
  `RendererHasTextures` stops the assert, but the font atlas still wants its
  texture created/updated. For a long-running headless harness (or a test that
  renders many frames), acknowledge texture requests each frame exactly like a
  renderer backend would. Measured effect: without this, the atlas texture
  stays permanently in a non-`OK` (pending) state every frame; with it, the
  pending count drops to 0:
  ```cpp
  ImGui::Render();
  ImDrawData* dd = ImGui::GetDrawData();
  if (dd && dd->Textures)
      for (ImTextureData* tex : *dd->Textures)
          if (tex->Status != ImTextureStatus_OK)
          {
              tex->SetTexID((ImTextureID)(intptr_t)(tex->UniqueID + 1)); // stand-in id
              tex->SetStatus(ImTextureStatus_OK);
          }
  ```
  This is the minimal texture contract a renderer must satisfy; a real renderer
  would instead upload `tex->GetPixels()` to the GPU (the snippet needs
  `#include <cstdint>` — gcc 11 with `-Werror` fails `'intptr_t' was not
  declared` in a plain C++17 TU without it; hit live building the headless
  probe for the Sep 2026 Paint A/B run). Pair with
  `io.AddMousePosEvent(0.0f, 0.0f)` so hover/`IsItemHovered` logic sees a
  concrete (not `-FLT_MAX`) position.

Verified build commands (no GLFW/SDL dev packages required for the first one):

```bash
# Headless smoke test — compile + run, proves your layout code is API-correct
g++ -std=c++11 -I. -Wall -Wextra -Wpedantic -Wshadow \
    templates/gallery_headless.cpp \
    imgui.cpp imgui_demo.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp \
    -o /tmp/gallery && /tmp/gallery

# Full GLFW + OpenGL3 app
g++ -std=c++11 -I. -Ibackends -Wall -Wextra -Wpedantic -Wshadow \
    templates/app_layout_glfw.cpp \
    imgui.cpp imgui_demo.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp \
    backends/imgui_impl_glfw.cpp backends/imgui_impl_opengl3.cpp \
    $(pkg-config --cflags --libs glfw3) -lGL -ldl -lpthread -o /tmp/myapp
```

Both templates ship with this skill and **build with zero warnings** under
`-Wall -Wextra -Wpedantic -Wshadow -Wformat=2`. Use
`scripts/build_headless.sh` as the fastest correctness gate.

## Widget API Quick Reference

Signatures below are exact for 1.92+. `label` is a UTF-8 `const char*`.

```cpp
// --- text ---
ImGui::Text("fmt %d", n);            // printf-style
ImGui::TextUnformatted("no fmt");    // no % parsing
ImGui::TextWrapped("long para");     // word-wrap to window width
ImGui::TextDisabled("greyed out");
ImGui::TextColored(ImVec4(1,1,0,1), "colored");
ImGui::Separator();
ImGui::SeparatorText("Section");     // labelled divider (preferred over Text+Separator)
ImGui::LabelText("Key", "%d", v);    // value with a label on the left

// --- buttons & toggles ---
bool ImGui::Button(const char* label, const ImVec2& size = ImVec2(0,0));
bool ImGui::SmallButton(const char* label);
bool ImGui::RadioButton(const char* label, int* v, int v_button);
bool ImGui::Checkbox(const char* label, bool* v);

// --- numeric ---
bool ImGui::SliderFloat(const char* label, float* v, float min, float max, const char* fmt = "%.3f", ImGuiSliderFlags flags = 0);
bool ImGui::DragFloat (const char* label, float* v, float speed = 1.0f, float min = 0.0f, float max = 0.0f, const char* fmt = "%.3f", ImGuiSliderFlags flags = 0);
bool ImGui::InputInt   (const char* label, int* v, int step = 1, int step_fast = 100, ImGuiInputTextFlags flags = 0);
bool ImGui::InputFloat (const char* label, float* v, float step = 0.0f, float step_fast = 0.0f, const char* fmt = "%.3f", ImGuiInputTextFlags flags = 0);

// --- text input (buf must be a writable array; size is capacity) ---
bool ImGui::InputText(const char* label, char* buf, size_t buf_size, ImGuiInputTextFlags flags = 0);
bool ImGui::InputTextMultiline(const char* label, char* buf, size_t buf_size, const ImVec2& size = ImVec2(0,0), ImGuiInputTextFlags flags = 0);
bool ImGui::InputTextWithHint(const char* label, const char* hint, char* buf, size_t buf_size, ImGuiInputTextFlags flags = 0);

// --- selection / lists (prefer the Begin/End forms) ---
bool ImGui::Selectable(const char* label, bool selected = false, ImGuiSelectableFlags flags = 0, const ImVec2& size = ImVec2(0,0));
bool ImGui::Selectable(const char* label, bool* p_selected, ImGuiSelectableFlags flags = 0, const ImVec2& size = ImVec2(0,0)); // read-write helper
bool ImGui::BeginCombo(const char* label, const char* preview_value, ImGuiComboFlags flags = 0); // EndCombo() iff true
bool ImGui::BeginListBox(const char* label, const ImVec2& size = ImVec2(0,0));                    // EndListBox() iff true
bool ImGui::MenuItem(const char* label, const char* shortcut = NULL, bool selected = false, bool enabled = true);

// --- hierarchy ---
bool ImGui::TreeNode(const char* label);                 // pushes ID; TreePop() iff true
bool ImGui::CollapsingHeader(const char* label, ImGuiTreeNodeFlags flags = 0); // NO TreePop needed
bool ImGui::BeginMenu(const char* label, bool enabled = true);   // EndMenu() iff true
void ImGui::SetNextItemOpen(bool is_open, ImGuiCond cond = 0);

// --- layout ---
void ImGui::SameLine(float offset_from_start_x = 0.0f, float spacing = -1.0f);
void ImGui::NewLine();
void ImGui::Spacing();
void ImGui::Dummy(const ImVec2& size);
void ImGui::Indent(float indent_w = 0.0f);  void ImGui::Unindent(float indent_w = 0.0f);
void ImGui::BeginGroup();  void ImGui::EndGroup();
void ImGui::BeginDisabled(bool disabled = true);  void ImGui::EndDisabled();
void ImGui::SetNextItemWidth(float item_width);
void ImGui::PushItemWidth(float item_width);  void ImGui::PopItemWidth();
ImVec2 ImGui::GetContentRegionAvail();          // "THIS IS YOUR BEST FRIEND"
float  ImGui::GetFrameHeight();                 // FontSize + FramePadding.y*2
float  ImGui::GetFrameHeightWithSpacing();      //   + ItemSpacing.y
float  ImGui::GetFontSize();                    // scaled size — do NOT pass to PushFont()

// --- popups / tooltips ---
bool ImGui::BeginPopupModal(const char* name, bool* p_open = NULL, ImGuiWindowFlags flags = 0); // EndPopup() iff true
void ImGui::OpenPopup(const char* str_id);
void ImGui::CloseCurrentPopup();
bool ImGui::IsItemHovered(ImGuiHoveredFlags flags = 0);
void ImGui::SetTooltip("fmt %s", s);
void ImGui::BeginTooltip();  void ImGui::EndTooltip();
```

For containers (window/child/table/tabbar/popup), see
`references/api-quickref.md` which also carries the flag enums and the
`Begin*/End*` pairing matrix.

## The Layout Idiom That Works

Wrap the whole GUI in one window, split it with a `BeginChild` pane + a table
for the property column. This is what the demo's "Simple layout" and "Property
editor" examples do, and it scales without hardcoded pixel arithmetic.

```cpp
static float f; static int sel; static char name[128] = "mesh_001";

ImGui::BeginChild("left", ImVec2(180, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
for (int i = 0; i < 10; i++)
{
    char lbl[64]; snprintf(lbl, IM_COUNTOF(lbl), "Object %02d###obj%02d", i, i);
    if (ImGui::Selectable(lbl, sel == i)) sel = i;
}
ImGui::EndChild();

ImGui::SameLine();

if (ImGui::BeginTable("props", 2, ImGuiTableFlags_SizingStretchProp))
{
    ImGui::TableSetupColumn("field", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted("Name");
                           ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##name", name, IM_COUNTOF(name));
    ImGui::EndTable();
}
```

Sizing rules worth memorising:

- `ImVec2(0, 0)` = use default/auto size. `-FLT_MIN` as a **width** = "fill
  available width". A **negative** height (e.g. `-ImGui::GetFrameHeightWithSpacing()`)
  = "leave that much room" — used to pin a button row under a scrolling child.
- Express sizes as multiples of `ImGui::GetFontSize()` / `GetFrameHeight()`, not
  hardcoded pixels, so DPI and font changes stay correct.
- `SetNextItemWidth(-FLT_MIN)` makes a widget fill its cell/row. `PushItemWidth`
  sets it for a block.

## IDs and Labels (avoid the most common user mistake)

Widget labels are **also the widget's identity**. The same label twice in one
scope = ID collision (the second widget silently operates the first).

| Want | Write |
|------|-------|
| Same visible label, distinct IDs | `"Play##foo1"`, `"Play##foo2"` — `##` hides the suffix |
| No visible label, but an ID | `"##On"` (empty label is **not** allowed) |
| Label changes, ID stays | `"Enable###MyButton"` / `"Disable###MyButton"` — `###` resets the ID |
| Loop / dynamic rows | `ImGui::PushID(i); ...; ImGui::PopID();` |

- An **empty label `""`** collides with the parent window — always use `"##..."`.
- `TreeNode`, `BeginMenu`, `BeginCombo` etc. push IDs for you (scoped).
- Debug collisions with `ImGui::ShowIDStackToolWindow(bool* p_open = NULL)`.

## Common Pitfalls

1. **Closing a container when its `Begin*` returned false.**
   `Begin`/`End` and **`BeginChild`/`EndChild` always pair, even when the Begin
   returned false** — these two are the documented exceptions. For everything
   else (`BeginPopup`/`BeginPopupModal`/`BeginTable`/`BeginCombo`/`BeginListBox`/
   `BeginTabBar`/`BeginTabItem`/`BeginMenuBar`/`BeginMenu`/`BeginTooltip`/
   `BeginMainMenuBar`/`TreeNode`), call the matching `End*` **only when it
   returned true**. Getting `BeginChild` wrong asserts
   `"Must call EndChild() and not End()!"` — it does **not** fail to compile.

2. **Pre-1.92 font code on a 1.92+ tree.** Glyph ranges are obsolete;
   `AddFontFromFileTTF(path)` needs no size; `PushFont` needs an explicit size
   (`PushFont(NULL, size)`); `IM_ARRAYSIZE` → `IM_COUNTOF`. Full list in
   `references/version-1.92-changes.md`.

3. **`PushFont(NULL, ImGui::GetFontSize())`.** Wrong — that size already had
   global scale factors applied, so they get applied **twice**. Use
   `PushFont(NULL, style.FontSizeBase)` (or a multiple of it).

4. **Passing a `float` array to `InputText`.** `InputText` takes `char*` + a
   `size_t` capacity, not a float. Use `SliderFloat`/`DragFloat` for floats.

5. **Forgetting `GetContentRegionAvail()` exists** and hardcoding window sizes,
   then items overflow on resize. Read avail space; or use auto-resize.

6. **ID collisions in loops.** `Selectable("Row")` × N. Use `PushID(i)` or the
   `###`/`##` trick.

7. **Duplicated `ImGui::` round-trips because the label is used as fmt.**
   `Text("100%")` is a printf string — a literal `%` misbehaves. Use
   `TextUnformatted` or escape as `%%`.

8. **Unbalanced style/item stacks.** Every `PushStyleColor`/`PushStyleVar`/
   `PushItemWidth`/`PushID`/`PushFont`/`PushTextWrapPos` needs its `Pop`.
   Mismatches assert at frame end.

9. **Calling ImGui outside the NewFrame()/Render() window.** All UI calls must
   happen between `NewFrame()` and `Render()`.

10. **Modifying `style` after `NewFrame()` without push/pop.** Use
    `PushStyleVar`/`PushStyleColor` inside the frame.

    **Debugging pairing/structure bugs:** most of the above assert at frame-end,
    never at compile time. To force the edge branches and actually see them, use
    Dear ImGui's own debug hooks (set before the loop or per-frame):
    ```cpp
    io.ConfigDebugBeginReturnValueOnce = true;  // 1st Begin/BeginChild returns false
    io.ConfigDebugBeginReturnValueLoop = true;  // cycles window depths -> returns false
    ```
    These make `Begin`/`BeginChild` return false so you can confirm your End/pop
    logic is balanced on the false branch. Pairs well with
    `io.ConfigDebugIsDebuggerPresent = true` (break on ID conflicts) and
    `ImGui::ShowIDStackToolWindow()` (inspect the ID stack live).

11. **Headless `NewFrame()` asserts instead of failing to compile.** A template
    dropped into a no-backend context asserts rather than erroring at build
    time unless you set `io.DisplaySize`, `io.DeltaTime`, **and**
    `ImGuiBackendFlags_RendererHasTextures`. The two asserts come from
    `ErrorCheckNewFrameSanityChecks()` (DisplaySize) and
    `ImFontAtlasUpdateNewFrame()` (font atlas). `templates/gallery_headless.cpp`
    sets all three — copy that init block verbatim for headless CI.

12. **Trailing backslash in a `//` comment.** Writing a multi-line shell command
    inside a C++ `//` comment with `\` line-continuations triggers `-Wcomment`,
    which fails under `-Werror`. When you document a build command in a template
    header, keep every comment line free of a trailing `\`.

13. **Dead code fails `-Werror` in strict builds.** Under
    `-Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Werror` (the build the skill
    recommends), two trivial mistakes break the build:
    - **`unused-function`**: a `static` helper you wrote but then stopped
      calling (e.g. a `CompareStudents()` comparator replaced by inline logic).
      `static` + unused = hard error. Delete it, or stop marking it `static` if
      it is genuinely part of the API.
    - **A write the loop overwrites**: e.g. a toolbar/menu handler sets
      `m.frame_count = 9999` to "signal exit", but the per-frame loop then does
      `m.frame_count = frame;` unconditionally, so the signal is dead. Prefer a
      separate `bool quit_requested;` the loop actually reads, not a reused
      counter.
    Neither is an ImGui bug — both are ordinary C++ hygiene. The compile-time
    gate catching them is the point: run `scripts/build_headless.sh` (it compiles
    with `-Werror`) rather than a plain `-Wall` build, and fix the first error
    before assuming an ImGui signature is wrong.

14. **`memset` on a struct containing `ImVec2` / `ImU32` members.** Under
    `-Wclass-memaccess` (in `-Wall`), `memset(&node, 0, sizeof(node))` is a hard
    error once the struct has *any* non-trivial member (e.g. an `ImVec2 pos;`
    field): *"clearing an object of non-trivial type 'struct Node'"*. Fix by
    assigning each member, or by making the struct trivially-copyable and using
    value-initialisation. Two more `-Wall` traps that bite node/editor code:
    - **`unused-but-set-variable`**: `ImVec2 cmin = ImGui::GetWindowPos();`
      computed then never read (very easy to leave behind while iterating on
      layout code). Delete it.
    - **`fmodf` / math functions**: `-Werror=implicit-function-declaration` if
      `<math.h>` is missing; include it explicitly.

> **Pitfalls 1, 2, 12 and 13 are empirically validated.** In a controlled A/B
> test (one agent with this skill, one with only the library source), the
> no-skill arm independently hit Pitfall 12 (`-Wcomment` from a trailing `\` in
> a `//` comment) and the `unused-function` half of Pitfall 13, needing two
> failed compile cycles to reach the same working build the skill arm reached on
> its first attempt. Both arms' final artifacts scored identically, so the
> skill's measured value is *fewer iterations*, not a different endpoint.
>
> Pitfall 14 was hit while building the node-graph example in this skill's
> `references/node-graphs.md` — the `-Wclass-memaccess` / `unused-but-set`
> pair fired on real editor code, confirming it belongs here.

15. **Do not put unimplemented features in toolbar hints or docs.** Writing
    "Alt+drag to detach wires" in a hint string when the code does not handle
    Alt is a silent lie that wastes the user's time testing a non-feature.
    Either implement it or remove the hint. (Cheap to write, expensive to
    debug from the user's side.)

16. **A fill-width child (`ImVec2(-FLT_MIN, 0)`) in a SameLine row swallows the
    rest of the row.** In a Hierarchy | canvas | Inspector layout, giving the
    middle canvas `{-FLT_MIN, 0}` width makes it consume ALL remaining width
    and pushes the right-hand panel entirely off-window — with no error, no
    assert, and a clean exit code. Split row widths explicitly (canvas =
    `GetContentRegionAvail().x` minus the fixed side panels) and verify with a
    screenshot, not just a successful build. See
    `references/game-editor-apps.md` for the full three-panel recipe.

- **Deferred mutations: never mutate a vector while iterating it in UI code.**
  A context-menu item, tab X, or card button runs *inside* the tree/tab/card
  loop; erasing from the vector there invalidates everything still in flight.
  Pattern (dreamIDE): handlers call `Defer(action)` (push to a static queue);
  `Draw()` flushes it after all containers closed:
  `for (const Action& a : g_deferred) Apply(a); g_deferred.clear();`
  Same rule for tab close: record the id from `BeginTabItem`'s `&opened ==
  false`, apply CloseDoc after `EndTabBar`. Full recipe:
  `references/ide-workspace-apps.md`.
- **Typed-input guard for shortcut chords.** A chord handled with
  `IsKeyChordPressed` fires even while the user is typing in an InputText
  (Ctrl+P mid-search opens the palette). Guard chords with
  `io.InputQueueCharacters.empty()` before acting (see
  `references/ide-workspace-apps.md` §Shortcuts).

### Node-graph pitfalls (see `references/node-graphs.md`)

16. **Storing node positions in screen space.** Pan/zoom then rewrites the model
    every frame and the graph drifts. Keep positions in graph space; convert at
    the boundary with `ToScreen`/`ToGraph`.
17. **Zoom not anchored at the cursor** — the view slides toward the origin.
    Re-derive pan from the graph-space point under the mouse before/after the
    zoom change (recipe in `node-graphs.md` §2).
18. **Hit-test order in a canvas**: submit the background `InvisibleButton`
    *before* the per-node buttons, or the background swallows node clicks. Pass
    `ImGuiButtonFlags_MouseButtonLeft | _MouseButtonMiddle` to the background.
19. **Pin anchors computed by a different formula than layout** — wires miss the
    connector circles. Share one `Node_InputPinPos`/`Node_OutputPinPos` helper.
20. **Nodes are repeated widgets**: wrap each node's `InvisibleButton`s/IDs in
    `PushID(node_index)`, or every node's "##body"/"##in"/"##out" collides and
    one node's pins drive another's (asserts at frame end).
21. **`io.MousePos` is `(-FLT_MAX,-FLT_MAX)` when the mouse leaves the window** — a live wire flies to an absurd coordinate. Guard with `IsMousePosValid()`.

    ### Build-system & engine-interop pitfalls (first hit: 2D editor/engine build)

    22. **Relative imgui source paths in CMake don't resolve.** `add_executable(app ../imgui/imgui.cpp ...)` fails with `Cannot find source file: ../imgui/imgui.cpp` followed by a confusing `No SOURCES given to target` — relative source paths resolve from the **build** directory, not the source dir. Fix: `set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../imgui)` and list `${IMGUI_DIR}/imgui.cpp` etc. Also verify the checkout root first (`ls <dir>/imgui.h` must exist AND `<dir>/backends/` must exist) — nested same-named folders (`Skills/imgui/imgui/`) cost two wasted configure cycles.
    23. **Raw GLFW key codes don't fit the 1.92+ key API.** `ImGui::IsKeyPressed(GLFW_KEY_SPACE)` → `error: invalid conversion from 'int' to 'ImGuiKey'`. Use `ImGuiKey_Space` / `ImGuiKey_*` enums for ImGui key queries; keep raw `GLFW_KEY_*`/`glfwGetKey` for your own input structs.
    24. **`ImGui::SeparatorEx` / `ImGuiSeparatorFlags_Vertical` are internal** (`imgui_internal.h`), not public API. For a vertical divider in a toolbar use `ImGui::Text("|")` between `SameLine()`s (or knowingly include `imgui_internal.h`). Same trap, bigger blast radius: **`ImClamp`, `ImMin`, `ImMax` are also `imgui_internal.h`-only** (imgui_internal.h:515-517 in 1.93.0 WIP) — against `imgui.h` alone you get a hard `'ImClamp' was not declared in this scope` under `-Werror`. On C++17 use `std::clamp` / `std::min` / `std::max`. (Hit independently by BOTH arms of a two-process A/B eval, Sep 2026 — this is the most commonly re-hit symbol trap after fonts.)
    25. **ImDrawList has `AddPolyline`, not `Polyline`.** Signature: `AddPolyline(const ImVec2* pts, int count, ImU32 col, ImDrawFlags flags, float thickness)` — closed shapes want `ImDrawFlags_Closed`. Your own math `Vec2`/`Color` structs will NOT implicitly convert to `ImVec2`/`ImU32` (`cannot convert 'Dodo::Vec2' to 'const ImVec2&'`); have transform helpers return `ImVec2` directly.
    26. **Const-correct engine structs or const readers won't compile.** Gizmo/render helpers that take `const World&` need a `const GameObject* Find(int id) const` overload — a single non-const `Find` makes every const consumer a hard error (`passing 'const World' as 'this' argument discards qualifiers`).

    ### Game-sim verification pitfalls (first hit: platformer + AI-judge build)

    27. **A capture harness launches a SECOND app instance — and corrupts
    determinism comparisons.** `capture_for_vision.sh` starts its own copy of
    the binary; an instance left over from an earlier test then runs
    concurrently, and the log lines you read came from a different process
    than assumed (GUI replay 1424 frames vs selftest 1242 — same binary, same
    input script). Always kill stale instances before capturing; both capture
    scripts now do it themselves (`pkill -xf "$BIN"`). **Field fix (dreamIDE,
    Sep 2026): they previously used `pkill -f "$BIN"`, which matched the
    script's OWN command line (the binary path is an argv to the script) and
    SIGTERMed the script itself — silent exit -15 right after
    `== launching ==`. Exact match `-xf` avoids the self-kill.**
    See `references/platformer-verification.md`.
    28. **The default ImGui font lacks em-dash/arrow glyphs — they render as
    `?`.** Log panels full of `—` / `→` show literal question marks ("(1242
    frames) ? replaying..."). Keep agent-built UI log strings ASCII, or load
    a fuller font at init. Also keep HUD text over an opaque chip: light text
    on a bright banner is unreadable in screenshots.

    29. **Right-aligned status text clipped off the right edge.**
    `ImGui::SameLine(ImGui::GetContentRegionAvail().x)` places the NEXT item's
    START at the right edge, so the text renders outside the window — the
    build is clean and the left half of the status bar still looks fine.
    Right-align properly by measuring first:
    `ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(s).x)`
    then emit the item. (First hit: NOTEPAD-80 build, Sep 2026 — compile and
    histogram both passed; only the vision loop caught the clipped
    "Loaded ... | N chars M lines".)

    30. **Hand-rolled socket reply formatting trips `-Werror=format-truncation`.**
    `snprintf(buf, n, " %s=(%.0f,%.0f)", name, x, y)` is a hard error even
    when `name` is a `char[64]` member — gcc sums worst cases (%s unbounded +
    float max widths) and rejects against the destination. Fix: explicit
    precisions matching real field maxima (`" %.63s=(%.0f,%.0f)"`,
    `"%.200s"` for echoed input). Related C++ trap that bit the same file:
    `struct Action` + `inline void Action(...)` in one namespace — the
    *ordinary* name (the function) shadows the tag inside the function body
    ("must use struct tag"). Name record types distinctly (`ActionRec`).
    (First hit: visual-verification harness, Sep 2026.)

    ### 1.92.8+/1.93 tree traps (first hit: headless Paint app build, Sep 2026)

31. **`ImClamp` is NOT visible from `imgui.h` on a 1.93.0-WIP (19297) tree** —
    hard error `'ImClamp' was not declared in this scope` under the strict build
    (it lives in `imgui_internal.h`). Use a ternary or `std::clamp` from
    `<algorithm>`. (`ImMin`/`ImMax` were observed working from the public header
    in a sibling build, but don't bet a cycle on them — ternaries always compile.)
32. **`ImDrawList::AddRect` 1.92.8 overload flip makes full-arg calls ambiguous.**
    New signature is `(p_min, p_max, col, rounding, thickness, flags)`; the
    obsoleted legacy overload keeps the old `(…, rounding, ImDrawFlags, float)`
    order. `AddRect(a, b, col, 0.0f, 0.0f, 1.0f)` → `call of overloaded … is
    ambiguous` (hard error). Call `AddRect(a, b, col)` and let trailing args
    default, or pass at most 5 positional args.
33. **`ImGuiConfigFlags_NoSettings` does not exist on 1.93.0 WIP.** `io.IniFilename
    = NULL` is the complete ini/settings disable — verify against the tree's enum
    before reaching for a flag name remembered from older code. Related: the
    headless texture-ack snippet above needs **`#include <cstdint>`** for
    `intptr_t` — without it, `-Werror` fires `'intptr_t' was not declared`.

34. **Pixel-exact self-test asserts: validate geometry offline before compiling.**
    Two silent algorithm bugs cost zero compile cycles because both were found in a
    Python port of the plot loop first: (a) a "Bresenham variant" with `err = dx+dy`
    and `e2 >= dy`/`e2 <= dx` advances x and y in the same step at slope 0.5 (wrong
    slope) — use the canonical `err = (dx>dy?dx:-dy)/2`, advance x when
    `e2 > -dx`, y when `e2 < dy`; (b) textbook midpoint ellipse dropped its
    right-axis pixel at the region-1→2 transition (plotted x reached cx+34 of
    rx=40) — perimeter-proportional angle sampling plus four explicit axis points
    is safe and testable. And when a self-check FAILs, suspect **your own check
    coordinates** too: the line (40,80)→(160,140) passes exactly through
    (100,110), not (100,109). Full recipe: `references/headless-selftest-apps.md`.

35. **Shared workdirs get clobbered by concurrent agents (A/B evals).** When a
    patch fails with *"modified by sibling subagent 'sa-…'"*, another process is
    overwriting your path. What worked: re-establish your file with `write_file`
    (full content, not patch), compile+run it in the SAME shell step to shrink the
    race window, keep a byte copy outside the contested dir (`cp src /tmp/mine.cpp`)
    and prove final state with `cmp src /tmp/mine.cpp`. A shared journal
    (BUILDLOG.md etc.) that gained the sibling's entries: never delete them —
    append a dated corrective provenance note citing file mtimes. Tool quirk
    hit live: a heredoc whose *body text* contains `&&` can trip the terminal
    guard ("Foreground command uses '&' backgrounding"); write the append text
    with write_file and `cat file >> target` instead.

        ### Verification & layout pitfalls (first hit: village-sim build, Sep 2026)

    36. **Coincident entities make nearest-wins click tests nondeterministic.**
        Two villagers walked to the same node, occupied the identical world
        position, and the canvas hit-test (`dd < best*best`, strict) picked
        whichever entity's coords survived the w2s→s2w round-trip with smaller
        float error — selection flickered between runs with the SAME seed and
        state. Fix: epsilon tie-break so the lowest index wins among visually
        coincident entities: `if (dd < best_sq - 1e-4f) { best_sq = dd; pick = i; }`
        (epsilon ≈ 0.01 world units squared ≪ 1 px, so it only kills the tie race).
        Same guard for hover tooltips.

    37. **A fill-height canvas item + appended bottom panel silently overflows
        the child (Pitfall 16's vertical twin).** `InvisibleButton(avail())`
        followed by a log child below → content taller than the child → a silent
        SCROLLBAR and the log scrolled out of view with zero errors; worse, the
        scrollbar/avail oscillation rescales a letterboxed view between frames,
        so harness-registered click rects drift and closed-loop selection flakes.
        Fix: explicit vertical split —
        `canvas_h = avail.y - log_h - ItemSpacing.y`. **Meta-lesson:** the vision
        model flagged "a vertical scrollbar" and "log integrated in the right
        dashboard" in the FIRST capture and the lead was dismissed as a quirk.
        Unexpected UI furniture in a vision read is evidence of a layout bug.

    38. **Contact-sheet vision verdicts on 2-3 px entities are unreliable.** A
        4-frame grid of a RUNNING village sim was judged "frozen, not even a
        pixel" because villagers are dots at tile scale. Prove motion numerically:
        mirror entity positions as harness float vars (`VarFloat("vx", &v[0].x)`),
        sample twice, assert delta; gate pixel change with
        `compare -metric RMSE a.png b.png null:` pairs — running pair > 0, paused
        pair **exactly 0** (bit-identical freeze: the strongest pause proof there
        is). See `references/village-sim-verification.md`.

    39. **Shared X displays corrupt driven verification.** Sibling automation
        keystrokes toggled the app's Space=pause shortcut while the window held
        focus (paused flipped with NO command sent), and `xdotool windowactivate`
        blocked ~13 s behind focus-stealing protection — visible as a frame-counter
        jump. Guards: re-assert model state via harness `set` around capture steps
        (`ensure_paused`/`ensure_running` from `get paused`), never wire keyboard
        shortcuts into loop dependencies, read frame-counter gaps as external
        blocking, not app failure. Probe hygiene from the same run: compare
        parsed values in numeric probes, never whole protocol replies (replies
        embed frame numbers, so a string-compare reports "changed" every time);
        and a scripted closed loop is not bit-deterministic across runs
        (real-time sleeps + click-delivery jitter shift the sim timeline) —
        assert invariants and state-vs-pixel agreement, not exact per-step states.

    40. **A selftest must FORCE the world state that exercises the mechanic.**
        `tool_breaks` failed 18/19 because the sim hit steady state — stocks at
        reserves → nobody harvests → tools never wear. Drain the stockpile in-test
        (`s.stock_wood = 0;`) before the run_until. Related UI honesty: Step must
        advance enough sim-time to be observable in state (1 sim-sec wasn't; 5 is)
        and be labelled truthfully ("Step 5s" — Pitfall 15).

41. **`LabelText` clips long labels at ~half the panel width.** In a ~255 px
    inspector, "Intelligence" rendered as "Intelligenc" (and "Intelliger" at
    230 px) because LabelText reserves only half the row for the label — a
    quieter sibling of Pitfall 29's right-edge clipping; the build, histogram
    and layout captures were all clean, only the vision transcript showed it.
    Fix for long labels in narrow panels: skip LabelText and emit aligned
    plain text — `ImGui::Text("Intelligence %.1f", v)` — the monospace default
    font keeps value columns aligned and Text never clips. Related: treat
    vision reads as proofreading passes too — the same capture surfaced
    "crafted a axe" in the event log (article-aware log strings fix it).

    ## Verification Checklist

- [ ] `grep IMGUI_VERSION_NUM imgui.h` — confirmed which API era you're in.
- [ ] Compiles clean: `bash scripts/build_headless.sh` (0 warnings, runs). It
      uses `-Werror`, so dead code / unused statics fail here, not silently.
- [ ] Every `Begin`/`End` pair balanced per the true/false rule above
      (`BeginChild` always closes, even on a false return).
- [ ] Every pushed stack (ID/style/item-width/font) has a matching pop.
- [ ] No two widgets share a label in the same scope (loops use PushID).
- [ ] **Current-API names**: `IM_COUNTOF` not `IM_ARRAYSIZE`; `BeginCombo` not
      `Combo`; `BeginListBox` not `ListBox`; no `GetGlyphRangesXXX()` on a
      1.92+ tree. Run the audit grep in `references/version-1.92-changes.md`.
- [ ] **Stable IDs** where the visible text changes or repeats: `###id` to keep an
      ID while the label varies, `##id` to hide a suffix, `PushID` in loops.
- [ ] Sizes expressed relative to font/frame height, not magic pixels.
- [ ] **1.92.8+ traps clear**: no `ImClamp` from public headers, `AddRect` called
      with defaulted trailing args (not 6 positional floats), `<cstdint>` included
      for the headless texture-ack snippet, no assumed flags (`NoSettings` is gone
      on 1.93) — see Pitfalls 31–33.
- [ ] **Pixel-exact geometry asserts validated offline** (Python port of the plot
      loop) before the first compile — see Pitfall 34.
- [ ] No dead code (no unused `static` helpers, no writes the frame loop
      overwrites, no `unused-but-set` locals) — see Pitfalls 13 & 14.
- [ ] No `memset` on a struct with `ImVec2`/`ImU32` members (`-Wclass-memaccess`)
      — see Pitfall 14.
- [ ] Lifecycle order: renderer `_NewFrame` → platform `_NewFrame` → `NewFrame`
      → `Render` → `RenderDrawData`; shutdown reverses init.
- [ ] Headless: `DisplaySize` + `DeltaTime` set per frame, `RendererHasTextures`
      set, and (long runs) texture requests acknowledged each frame.
- [ ] **GUI builds actually render** — a clean compile + exit 0 does NOT mean
      pixels appeared. On a machine with an X display, run
      `bash scripts/verify_gui_render.sh ./myapp "My Title"` and confirm the
      colours your draw code emits (`IM_COL32(...)` literals) show up in the
      histogram. Two independent signals beat one: process stays alive AND the
      expected palette is in the captured window.
- [ ] **Layout is RIGHT, not just present** — the histogram cannot see a panel
      pushed off-window or clipped text. For any non-trivial layout, run
      `bash scripts/capture_for_vision.sh ./myapp "My Title"` and pass the
      printed screenshot path to the vision tool with a question naming the
      expected panels (see "The Vision Loop" below). Three signals: compiles,
      pixels exist, layout looked at.
- [ ] **Interactive states are REACHED and seen, not assumed** — for apps with
      buttons/toggles/modes, wire `templates/vision_loop/imgui_harness.h`,
      run a `closed_loop.sh`-style sequence (action → `sync` settle →
      capture), and confirm the `state=` line matches the commanded actions
      AND the vision read of each settle-synced capture matches `state=`
      (`references/automated-visual-verification.md`).
- [ ] **For games/sims, MOTION is verified, not assumed** — a static capture
      cannot show the player moving or counters ticking. Run
      `bash scripts/capture_frames_grid.sh ./myapp "Title" 4 2` and give the
      tiled contact sheet to the vision tool with a motion question — but for
      small entities (2-3 px dots) contact sheets read as "frozen"; confirm
      with numeric position mirrors or pixel RMSE pairs (running > 0, paused
      == 0; see `references/village-sim-verification.md`). For the sim itself,
      assert determinism with a headless replay selftest
      (`references/platformer-verification.md`).
- [ ] **For SVG-derived code, per-shape liveness is verified** — a conversion
      that emits zero draw calls still compiles clean and exits 0. Probe
      `VtxBuffer` deltas per `DrawSvg_*` at two times (pattern in
      `templates/svg/svg_widgets.cpp`), then confirm pixels in a WINDOWED run:
      `<text>` conversions false-negative headless (unbaked font size emits no
      vertices). See `references/svg-for-imgui.md`.

## The Vision Loop (let the model SEE the GUI)

Compiles-clean + histogram only prove pixels exist; they cannot see that the
layout is *correct*. An agent has no eyeballs — give it some:

**This script is the ONLY sanctioned way to view a running app.** Do not
hand-roll alternate capture paths (ad-hoc launch+sleep+screenshot piped
straight into the vision tool): in the field (dreamIDE, Sep 2026) that route
burned iterations on vision_analyze timeouts and shell plumbing while the
script's own flow (launch → window capture → resize → printed path) is the
reliable one. Run `capture_for_vision.sh`, take the `_small.png` path it
prints, and pass exactly that to the vision tool.

```bash
bash scripts/capture_for_vision.sh ./myapp "Window Title" 3
# prints e.g. /tmp/imgui_vision/myapp_small.png
```

Then pass that path to the vision tool with a question that NAMES the expected
layout (vague questions get vague answers):

> "Describe this UI: which panels are visible and where (toolbar/left list/
> center canvas/right panel/bottom bar)? Is any text clipped, overlapping, or
> pushed off-window? Any empty panels or stray artifacts?"

For games and simulations, add the time dimension with
`capture_frames_grid.sh` (N frames over time, tiled into one image) and ask
about MOTION: is anything moving between frames, does movement match the
controls, are any frames frozen?

**Closing the loop (act → settle → capture → assert).** The capture scripts
above are one-way: they can look but not touch. For interactive verification,
see "The Closed Loop" subsection below.

This is how the three-panel layout bug below (Pitfall 16) was caught: the
build was clean, the histogram showed plenty of UI-coloured pixels, and only
the screenshot showed the Inspector had been pushed entirely off-window.
The frame grid caught a subtler one: two concurrent app instances made the
GUI's replay log disagree with the headless selftest (Pitfall 27).

Gotchas:
- Resize the capture to ~1024px wide first — same diagnosis, fewer tokens
  (`capture_for_vision.sh` does this automatically).
- Ask specific yes/no-checkable questions ("is the right panel visible?"),
  not "does this look right?".
- For layout A/B comparisons keep the same question text and swap only the
  binary — differences are then attributable to your change.
- If the app exits instantly under the capture, run it manually once; some
  apps need `glfwShowWindow` or a few frames before the window maps.
- **If a launch/capture command is DENIED (user or approval gate), do not
  retry it or route around it.** Vision verification does not require a
  fresh launch: fall back to evidence that already exists — prior captures
  under `/tmp/imgui_vision/`, screenshots saved in the transcript, or the
  app's own logged output — and state plainly which signal you could and
  could not re-run. (Hit live: a pkill+capture re-verification was denied;
  the existing session capture answered the same layout questions.)

### The Closed Loop (act → settle → look)

One-shot captures prove what a frame LOOKS like; they cannot drive the app
through states. For interactive/stateful verification, embed
`templates/vision_loop/imgui_harness.h` (~330 lines, header-only, POSIX UDS —
no new deps): the app registers flat model vars + the live rect of each
testable widget per frame; a driver (`drive.py` / `closed_loop.sh`) sends
line commands over the socket — `press <name>` injects a REAL 3-frame
mouse click at the widget's own rect (hit-testing genuinely exercised, no OS
coordinate guessing), `set`/`state` stage and read the model, `sync <frame>`
is a settle barrier so screenshots happen only ≥2 presented frames after the
action, `ping` replaces sleep-based launch readiness. Then each capture goes
to the vision tool with the step's expected state embedded in the question;
success = API assertion AND pixel read agree at every step. Full contract,
the GLFW mouse-re-queue ordering rule, and trade-offs vs ImGuiTestEngine/HTTP:
`references/automated-visual-verification.md`. Worked example:
`templates/vision_loop/` (loop_demo.cpp builds + ran the 4-step loop green,
2026-09-14).

## Scope Discipline (user preference — applies to skill-built apps)

When building apps/games/tools under this skill, the user wants a **pragmatic
minimalist**: plain structs over ECS, flat vectors over graphs, one file over a
framework, an explicit "out of scope" list in the plan, and a complexity budget
stated up front (e.g. 3/10). No undo/redo, no scripting layers, no asset
pipelines "for later". Every feature must pay for itself. State the out-of-scope
list in the plan and hold the line during implementation.

Two more standing instructions from this user, both repeated across builds:

- **Plan first, then implement.** "At first write a plan" — produce the
  bite-sized plan (plan skill, `.hermes/plans/`) BEFORE writing app code, and
  smoke-test the toolchain while planning so the plan rests on verified facts.
- **Keep them posted.** "Keep me touched" — during long autonomous builds,
  emit a brief progress note at each milestone (todo updates, one-line status
  per task, the path of anything they can run). Silence for an hour is the
  failure mode; interim updates are part of the deliverable.
- **Finalize in-session when the brief itself is a skill update.** The
  plan → build → review cycle gates the *app* artifact (they want a compiled
  binary to test before you bake patterns in), but do not hold the
  `~/.hermes/skills/` writeback indefinitely waiting for a test that may
  already be implied by a green validation run. Ship it and report; their
  "did you update the skill?" IS the go-ahead — act on it immediately rather
  than re-asking. (Vision-loop session, Sep 2026.)

## Session Log Maintenance (token_usage_statistics.txt)

The user keeps a `token_usage_statistics.txt` in this skill's directory and
asks for session status appended after skill-building runs. To fill it in
without guessing:

- Per-session ground truth lives in `~/.hermes/state.db`, table `sessions`:
  `id, title, started_at (unix), message_count, tool_call_count,
  api_call_count, input_tokens, output_tokens, cache_read_tokens, model`.
  Query via `python3` + `sqlite3` (no `sqlite3` CLI on this box). Sessions
  with the same title prefix chain across context resets (id
  `YYYYMMDD_HHMMSS_xxxxx`); sum their rows for the session's true total.
- `hermes insights --days 1` gives a quick cross-check (and top-tools/skills
  breakdown), but its numbers keep ticking while the session is live —
  record them as approximate.
- Append one `[Session N]` block (id, date, what was built, skill outcome,
  token breakdown) and update the cumulative footer; mark live-session
  numbers as approximate. Note WHICH pitfalls/features the run contributed
  back to the skill — that's the field-test evidence.

## References

- `references/headless-selftest-apps.md` — Spec-driven backend-free app pattern:
  model/Op layering (UI and self-test share one code path), `assert <name> PASS`
  stdout contract + summary line, independent PPM recount in Python, debug-flag
  `ConfigDebugBeginReturnValueLoop` binary as runtime End-pairing proof, compile
  ledger (what costs vs. saves cycles), BUILDLOG journal discipline incl. the
  concurrent-agent clobbering recovery (Pitfalls 31–35).
- `references/installing.md` — install/vendor ImGui: minimal 16-file GitHub-raw
  set, git clone, release tarball, backend pairs, the libimgui-dev apt trap.
- `references/api-quickref.md` — full widget/flag tables, pairing matrix,
  container APIs, style/colour constants.
- `references/version-1.92-changes.md` — every 1.92 API change that breaks
  copied tutorials. Read before pasting any online snippet.
- `references/patterns-from-examples.md` — the 10 canonical examples analysed,
  with the shared construction pattern.
- `references/sources.md` — authoritative upstream doc pointers (docs/ folder,
  imgui_demo.cpp, imgui.cpp PROGRAMMER GUIDE, wiki).
- `references/cmake-integration.md` — CMakeLists for imgui-as-sources + static local
  GLFW; toolchain-discovery probes; build pitfalls (relative paths, ImGuiKey vs GLFW keys,
  AddPolyline, ImVec2 conversions).
- `references/node-graphs.md` — node/dataflow graph editors: canvas + pan/zoom,
  typed pins, bezier wires, invisible-button interaction, public-API-only recipe.
- `templates/DreamIDE/` — Complete IDE-style workspace app ("Project Desk"):
  single-root-window shell (menu bar, toolbar), flat-vector project explorer
  tree, tabbed editor via `imgui_stdlib`, inspector, filterable console,
  Kanban board with overlay-text cards, Ctrl+P command palette, chord
  shortcuts with typed-input guard, Action-intent state model with deferred
  mutations. Builds zero warnings; renders verified. Worked example behind
  `references/ide-workspace-apps.md`.
- `templates/gallery_headless.cpp` — self-contained widget gallery, no backend.
- `templates/app_layout_glfw.cpp` — canonical GLFW+OpenGL3 app skeleton.
- `templates/node_graph_editor.cpp` — UE-Blueprints-styled node/dataflow editor
  (7 nodes, 7 links). Compiles GUI + headless self-test, zero warnings. The
  worked example behind `references/node-graphs.md`.
- `templates/LogicCircuitSimulator/` — Complete, production-grade Digital Logic
  Circuit Simulator in pure Dear ImGui (zero external libraries). Contains
  `CircuitData.h`, `CircuitEngine.h/cpp`, `CircuitEditor.h/cpp`, and `main.cpp`.
- `references/logic-circuit-simulator.md` — Deep architectural reference on 4-layer
  `ImDrawListSplitter`, canvas math, vector gate iconography, and wiring state machines.
- `references/game-editor-apps.md` — Full-window editor/game-app patterns: toolchain
  discovery on bare machines (static GLFW probing), CMake source-path resolution,
  three-panel editor layout (fill-width canvas child pushes panels off-screen),
  GLFW valid-key-range polling, headless engine logic tests.
- `references/platformer-verification.md` — Deterministic game-sim verification:
  fixed-timestep + `FrameInput` replay, bit-identical-replay selftests,
  telemetry serialization for an AI judge, strict-JSON verdict extraction with
  an explicit INCONCLUSIVE state, HTTPS-without-libcurl (OpenSSL+POSIX),
  procedural ImDrawList game rendering, the two-instance capture race.
- `references/village-sim-verification.md` — closed-loop hardening from the
  village-sim build: numeric motion mirrors + RMSE gating (frozen/paused
  proof), coincident-entity click tie-break, fill-height+appended-panel
  overflow fix, shared-display env guards, steady-state selftest forcing.
- `templates/game_editor_template.h` — Compile-tested 2D world/editor pattern:
  flat-vector GameObject world, Camera (+y up) with cursor-anchored zoom,
  ImDrawList shape rendering, rotated hit-testing, play-state snapshot
  controller, three-panel canvas recipe. Paired with
  `templates/game_editor_template_check.cpp` (8-check self-test, builds
  headless against imgui sources, zero warnings).
- `templates/PlatformerSim/` — Complete deterministic Mario-style platformer +
  AI-playability verification harness in pure ImGui (procedural ImDrawList
  visuals, FrameInput replay, telemetry JSON, OpenSSL HTTPS judge client,
  25-assertion headless selftest). Worked example behind
  `references/platformer-verification.md`.
- `scripts/build_headless.sh` — strict-warning build + run gate.
- `scripts/install_imgui_minimal.sh` — vendor the minimal ImGui file set (core
  + GLFW/OpenGL3 backend pair) from GitHub raw for any tag/branch; verifies the
  download and prints the ready-to-paste build command.
- `scripts/verify_gui_render.sh` — launch a GUI build, screenshot its window,
  and dump a colour histogram so you can confirm it ACTUALLY renders (the
  headless gate proves API correctness, not that pixels appear).
- `scripts/capture_for_vision.sh` — launch + screenshot + resize for a vision
  model to inspect layout ("the vision loop"); prints the exact path to pass
  to the vision tool plus a suggested question template. Kills pre-existing
  instances of the same binary first (determinism guard).
- `scripts/capture_frames_grid.sh` — capture N frames over time, timestamp and
  tile them into one contact sheet, so a vision model can verify MOTION
  (moving entities, camera scroll, ticking HUD) with a single call.
- `references/automated-visual-verification.md` — the CLOSED loop: UDS control
  protocol contract, frame-sync rules (GLFW re-queues OS mouse events every
  frame — inject after backends, before NewFrame; 3-frame hover/down/up
  clicks; `sync` settle barrier replaces sleeps), state-vs-UI action paths,
  harness-friendly app design, vision-question pairing, ImGuiTestEngine/HTTP
  trade-offs, -Werror pitfalls hit building it (Pitfall 30).
- `templates/vision_loop/` — reference implementation, built + validated
  2026-09-14: `imgui_harness.h` (drop-in control API), `loop_demo.cpp`
  (minimal app: counter/checkbox/gauge, every state visible ≥2 places),
  `drive.py` (stdlib socket client), `closed_loop.sh` (launch→act→sync→
  capture manifest), `build.sh` (strict, static-GLFW recipe).
- `references/ide-workspace-apps.md` — IDE/workspace-app patterns from the
  dreamIDE build: Action-intent single write path, deferred mutations (never
  mutate while iterating), fullscreen root-window shell, explorer tree from a
  flat parent_id vector, tabbed editor + imgui_stdlib + IsItemEdited dirty
  tracking, autoscroll console, Kanban card overlay text, command palette
  (SetKeyboardFocusHere, arrow/Enter nav, Enter+KeypadEnter), shortcut chords
  with InputQueueCharacters guard, live 1.92+ UI scaling via
  `style.FontScaleMain`.
- `references/headless-selftest-deliverables.md` — Headless graded deliverables: run-length
  ImDrawList canvas blit, PPM background-byte trap vs whitespace-tokenizing verifiers,
  debug-hook run as model-purity invariant, source-identity discipline in contested/shared
  output directories (parallel agents clobbering files).
- `references/session-log-mining.md` — how to fill token_usage_statistics.txt:
  state.db `sessions` schema + query pattern (sqlite3 via python3), chaining
  session rows across context resets, log-entry format.
- `references/svg-for-imgui.md` — SVG's two roles next to ImGui; the
  convertible-subset authoring rules; element→ImDrawList + SMIL→GetTime()
  mapping tables; the 10 empirically-hit limitations (baked-font AddText
  silence, dash patterns, ImVec2 operators, tessellation cost); verification
  recipe. Read before converting or hand-writing SVG-derived draw code.
- `scripts/svg2drawlist.py` — SVG→C++ converter (stdlib only): `--check` lint,
  `--tint` for recolorable monochrome icons, batch dirs, SMIL
  (values/keyTimes/keySplines/begin, rotate, path morph via arc-length
  resample, dash-window spinners). Unsupported constructs emit `// WARN:`,
  never silent drops.
- `templates/svg/` — reference implementation: 12 GUI-oriented 24×24 SVGs
  (8 icons + spinner/dots/progress/pulse), `svg_runtime.h` (loop/easing/color
  helpers, public API only), committed generated header `svg_icons_imgui.h`,
  `svg_widgets.cpp` (tinted icon buttons + live animated widgets; dual-mode
  GUI + `-DSVG_HEADLESS_SELFTEST`), `build.sh` (regenerate + strict-build both
  binaries; verified via the vision loop).
