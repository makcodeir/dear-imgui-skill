# dreamIDE — "Project Desk" implementation plan

Single-window ImGui desktop app, flat structs + actions, no frameworks.

## Layout (main.cpp)
GLFW + OpenGL3 backend, dark style. Each frame: `App::Draw(app)` (single root
window, menu bar inside) → modal popups → shortcuts → render.

## Files (4 code files + plan + build.sh)
- `Model.h` (~200): flat structs. FileNode tree, LogEntry, Task, Prefs,
  EditorDoc, App state (Selection variant as tagged union: None/File/Task).
  `Action` struct — the single write path for all UI intents.
  `App::Apply` implements intents (save/close/move/delete/rename/build/run/…).
- `App.h/.cpp` (~250): `App::Init` sample data (Project Alpha tree, 6 tasks,
  3 editable docs), `Apply` implementation, `ConsoleLog` helper, key-chord
  handler (with 100ms buffer drain, typed-input guard, Escape cascade),
  command registry + exec (View toggles, Save, Build, …).
- `Panels.cpp` (~700): menu bar, toolbar (Tooltips, config combo, dirty dot),
  explorer (tree via index stack, rename-in-place InputText, right-click
  menu), editor (tab bar, InputTextMultiline into doc, Find bar w/ match
  count, line/char count), inspector (table of controls, per-kind fields),
  console (severity filter checkboxes, colors, autoscroll, Clear, sim Build
  appends ~6 entries, Run → [ERROR] exit code 1), task board (4 columns via
  BeginTable, Add/Del, Move via context menu + >> buttons, card selection
  feeds inspector), command palette (modal, fuzzy filter, arrow/Enter),
  prefs modal (UI scale → style.FontScaleMain live, font sizes, theme combo,
  autosave, debug grid), about modal.
- `main.cpp` (~70): backend lifecycle, per-frame calls.
- `build.sh`: strict g++ build, imgui+stdlib+glfw+gl3 sources, static GLFW
  from DBSCAN build tree (proven path).

## Key decisions
- Editor edits `doc.text` via `ImGui::InputTextMultiline` with
  `imgui_stdlib` (available in misc/cpp) — no fixed char buffers, no
  dangling pointers into strings; selection state uses indices/IDs only.
- `Action` struct centralizes state mutation → UI stays declarative-ish
  without OOP machinery (complexity ~4/10).
- Shortcuts: `IsKeyChordPressed` (1.92+ API) once per frame; typed-input
  guard via `io.InputQueueCharacters` empty; drain queue each frame.
- Panel widths: left = 21% avail, right = 25%, editor fills rest; heights
  pinned with `-GetFrameHeightWithSpacing()`.
- Out of scope: persistence, real FS access, docking, undo, icon fonts
  (ASCII glyphs only — default font has no ▸/💾 glyphs).

## Verification
1. `build.sh` — zero warnings.
2. `scripts/verify_gui_render.sh` — window actually renders.
3. `capture_for_vision.sh` ×3 (main, palette, board) → vision tool checks
   layout, clipping, panel positions.
4. Headless smoke: 60 frames, all popups exercised, no asserts.
