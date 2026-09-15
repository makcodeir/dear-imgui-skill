# IDE-Style Workspace Apps (dreamIDE case study)

Findings from building dreamIDE ("Project Desk"), a single-window developer
workspace in pure Dear ImGui: menu bar, toolbar, project explorer tree,
tabbed code editor, inspector, filterable output console, Kanban task board,
command palette (Ctrl+P), preferences modal, keyboard shortcuts. Four code
files (~1000 lines total), no frameworks. Worked example:
`templates/DreamIDE/` (builds zero warnings, verified rendering 2026-09-14).

Complements `game-editor-apps.md` (three-panel canvas editors) — read that
for canvas/pan/zoom; this file covers the *workspace* side: document tabs,
trees, modals, palettes, deferred mutations.

## Architecture: flat model + Action intents (the single write path)

Split app state from UI. `Model.h` holds plain structs (FileNode tree as a
flat vector + `parent_id`, Task, LogEntry, Prefs, EditorDoc, a tagged-union
Selection by `kind` int). All UI intents go through one struct:

```cpp
struct Action {
    enum Type : int { None, NewProject, Save, Build, SelectFile, Delete, /* ~20 more */ } type;
    int panel = -1, id = -1, column = -1, command = -1;  // only what the intent needs
    std::string text;                                     // Rename / Config names
};
struct App { void Apply(const Action& a); /* ... */ };
```

Why it pays: widgets become declarative ("emit Action, done"), the delete
cascade and doc bookkeeping live in ONE place, and `ExecuteCommand` /
shortcuts / context menus all reuse `Apply` instead of duplicating logic.
`Apply` takes the Action by const ref; convenience ctor makes call sites
one-liners: `Apply({Action::Save})`, `Apply({Action::MoveTask, 0, id, col})`.
Complexity ~4/10 — no OOP machinery, no ECS.

Indices and IDs only — never store `FileNode*`/`Task*` across frames (vector
growth + deletion invalidate them). Selection stores `{kind, file_id,
task_id}`; widgets re-resolve via `FindFile(id)` each frame. Null-check the
resolve: a deleted selection renders "Selection no longer exists."

## Deferred mutations: never mutate while iterating

This is the load-bearing trick of the whole app. UI code runs *inside*
container iteration (a context-menu item inside a tree row, a card button
inside the task loop, the X on a tab). Mutating the underlying vector there
invalidates every pointer/index still in flight — including ImGui's own
begin/end state for the containers.

Pattern: menu/button handlers call `Defer(action)` (push to a file-scope
`static std::vector<Action>`); `App::Draw()` flushes the queue at frame end,
after every container has closed:

```cpp
// in Draw(), after all panels and modals:
for (const Action& a : g_deferred) Apply(a);
g_deferred.clear();
```

What must be deferred here: file Select/Duplicate/Delete/NewFile, task
Select/Move/Delete, tab CloseDoc. What may run inline: pure state flips with
no iteration in flight (panel toggles, checkbox writes, combo selection).

Same principle, other cases:
- **Tab close**: `BeginTabItem(&opened)` returning `opened == false` must not
  erase from `docs` mid-`BeginTabBar`. Record `g_pending_close_doc =
  d.file_id`, close the tab bar, THEN apply CloseDoc. Cache the tab count
  into a local before the loop (`const int count = (int)docs.size();`) — a
  close flag can also shift iteration.
- **Rename modal**: the context menu sets `g_rename_id` + prefills
  `g_rename_buf`; the modal's `OpenPopup` is re-issued each frame while
  pending (OpenPopup is idempotent-ish per-frame here; close pending on
  first true Begin). Enter-or-OK applies; Cancel just clears pending.

## Single-root-window workspace shell

One fullscreen root window (not a viewport overlay — plain child split, works
on any backend/version):

```cpp
const ImGuiViewport* vp = ImGui::GetMainViewport();
ImGui::SetNextWindowPos(vp->WorkPos);
ImGui::SetNextWindowSize(vp->WorkSize);
ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); // edge-to-edge
ImGui::Begin("##main", nullptr,
    ImGuiWindowFlags_NoTitleBar | NoCollapse | NoResize | NoMove
  | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus
  | ImGuiWindowFlags_MenuBar);
ImGui::PopStyleVar();
```

`WindowPadding(0,0)` + `ImGuiChildFlags_Borders` on the panels gives the
classic docked look with zero docking API. Menu bar inside the root window
(`ImGuiWindowFlags_MenuBar` + `BeginMenuBar`). `io.IniFilename = nullptr` for
a fixed layout (no imgui.ini churn between runs).

Vertical split (top content vs bottom console) is arithmetic, not splitters:

```cpp
float bottom_h = ImClampf(GetTextLineHeightWithSpacing() * 7.0f, 80.0f, avail_h * 0.45f);
const float content_h = panels.console ? avail_h - bottom_h - spacing : avail_h;
```

Panel widths: side panels as clamped fractions of available width —
`ImClampf(content_w * 0.21f, 150.0f, 340.0f)` left / `0.25f, 190, 400` right;
the center child uses `-FLT_MIN` fill. (Safe HERE because the center panel is
positioned between two fixed-width SameLine siblings, never the other way
round — the fill-width child must be LAST. See game-editor-apps.md for the
failure mode when it isn't.)

## Project explorer tree from a flat vector

Tree from `parent_id` without recursive data structures:

```cpp
void DrawChildren(int parent_id) {
    for (i over project.files) if (files[i].parent_id == parent_id) DrawRow(i);
}
// row: TreeNodeEx with Leaf|NoTreePushOnOpen for files; recurse + TreePop when open
```

Details that matter:
- `PushID(n.id)` per row; icon prefix `"[+] "` / `"[ ] "` as the TreeNode
  label (default font has no ▸/folder glyphs — Pitfall 28 in SKILL.md).
- `IsItemClicked() && !IsItemToggledOpen()` selects without colliding with
  expand/collapse; double-click-to-open via `IsItemHovered() &&
  IsMouseDoubleClicked(0)` — no OpenOnDoubleClick flag needed.
- `BeginPopupContextItem("##ctx")` per row gives right-click menus for free.
- `SpanFullWidth` so the highlight covers the row.

Rename-in-place (modal variant): context menu stashes `id + prefilled
buffer`, a modal with `InputText(..., ImGuiInputTextFlags_EnterReturnsTrue)`
applies on Enter or OK.

## Editor pane: tabs + stdlib text + live status

- `misc/cpp/imgui_stdlib.h` gives `InputText`/`InputTextMultiline` overloads
  taking `std::string*` — no fixed char buffers, no dangling pointers into
  strings. Link `imgui_stdlib.cpp`. (For the small per-row char fields in the
  inspector, plain `char buf[N]` + copy-back on `InputText` returning true is
  fine and simpler — pick per site, both shown in Panels.cpp.)
- Per-doc dirty tracking: `if (ImGui::IsItemEdited()) doc.dirty = true;`
  after the multiline widget; tab caption shows it via
  `ImGuiTabItemFlags_UnsavedDocument` (the little dot).
- `IsItemEdited()` also gates table-cell writes: `if (InputText(...)) write-back`
  fires only on change, not every frame.
- Status row: line count = `std::count` of `'\n'` + 1; right-align with
  `SameLine(GetContentRegionAvail().x - w)`.
- Per-pane font scale: `PushFont(NULL, (float)prefs.editor_font)` around the
  editor body (1.92+ dynamic font API — size in px, NULL font = base face),
  `PopFont` after. Console does the same with its own size.

## Output console

Nested scrolling child, severity color-coded `Text`/`TextColored` lines,
`show[4]` checkbox filter per severity, autoscroll only when the user is
already at the bottom:

```cpp
if (auto_scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
    ImGui::SetScrollHereY(1.0f);
```

Cap the ring (`entries > 4096 → erase front`) so a chatty log can't grow the
vector unbounded. Severity colors: warn `1,0.75,0.2`, error `1,0.35,0.35`,
success `0.3,1,0.4`.

## Kanban board: columns of cards

4 fixed-status columns via a `BeginTable`-free row of SameLine'd children:

```cpp
const float col_w = (GetContentRegionAvail().x - 3.0f * spacing) / 4.0f;
for (int col = 0; col < 4; ++col) {
    ImGui::PushID(col);
    ImGui::BeginChild("##col", ImVec2(col_w, -FLT_MIN), ImGuiChildFlags_Borders);
    /* cards whose status == col */
    ImGui::EndChild();
    if (col < 3) ImGui::SameLine();
    ImGui::PopID();
}
```

Each card is a bordered child of fixed height
(`2*line_h + 2*FramePadding.y + 8`), with a full-card `Selectable` for
click/double-click, `BeginPopupContextItem` for move/delete, and **overlay
text via `SetCursorScreenPos` back to `card_pos + (6,3)`** after the
selectable — the standard "widget as background, text on top" trick:

```cpp
const ImVec2 card_pos = ImGui::GetCursorScreenPos();
ImGui::BeginChild("##card", ...);
ImGui::Selectable("##sel", selected, ImGuiSelectableFlags_AllowDoubleClick, size);
/* context menu, then: */
ImGui::SetCursorScreenPos(ImVec2(card_pos.x + 6.0f, card_pos.y + 3.0f));
ImGui::TextUnformatted(t.title.c_str());
```

Clip overlay text to the card (`PushClipRect`/`PopClipRect`) so long titles
truncate at the border instead of spilling into the next column — truncation
without ellipsis is fine for cards. A thin `ProgressBar` with
`PlotHistogram` color push shows progress inline. Double-click advances the
card to the next column (`(status + 1) % 4`) — cheap, discoverable, demoable.

## Command palette (Ctrl+P) — the reusable modal pattern

Modal + auto-focused filter + filtered list + arrow-key selection:

```cpp
ImGui::SetNextItemWidth(-FLT_MIN);
if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();  // focus ONCE on open
ImGui::InputTextWithHint("##pfilter", "Type a command...", &filter);
// ... filter command names into shown[16] ...
if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) idx = (idx + 1) % n;
if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))   idx = (idx - 1 + n) % n;
if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
    { Execute(shown[idx]); CloseCurrentPopup(); }
```

- `SetKeyboardFocusHere()` before the input under `IsWindowAppearing()` —
  the palette is type-to-filter from the first keystroke.
- Entries need distinct IDs while labels repeat → `"Name (Ctrl+S)##cmd42"`.
- Filter with a case-insensitive substring matcher (tolower loop) — fuzzy
  scoring is not worth it for <100 commands.
- Position with `SetNextWindowPos(center, ImGuiCond_Appearing, pivot(0.5,0))`
  at 25% height; `AlwaysAutoResize` + `NoTitleBar` for the spotlight look.
- Enter must test `ImGuiKey_Enter` AND `ImGuiKey_KeypadEnter` — numpad Enter
  is a separate key.
- Arrow keys work *while* the InputText has focus: InputText doesn't consume
  Up/Down, so plain `IsKeyPressed` after the widget sees them.

## Shortcuts, chords, and the typed-input guard

`ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)` — the 1.92+ chord
API, one call per shortcut, checked once per frame in `HandleShortcuts()`
right after NewFrame, before Draw. Fires once per press, no manual
was-down bookkeeping.

Critical guard — **don't let a chord fire while the user is typing** in any
InputText (Ctrl+P in the middle of typing a find string must not open the
palette, single-letter chords like F5 aside). Test the queue before acting:

```cpp
if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_P) && io.InputQueueCharacters.empty())
    RequestCommandPalette();
```

The palette/prefs Escape cascade: one `HandleEscape()` consulted by the
Escape handler — close the topmost open modal first (palette > prefs >
about), one per press. Modal open requests are bools set by
shortcuts/menus, consumed by `OpenPopup` in `Draw()` (never call OpenPopup
from inside a menu item — the popup system needs it issued from the window
that owns the popup, and once-per-frame).

## Preferences: live font scaling without font rebuilds

1.92+ makes UI scaling a slider — no atlas rebuild, no per-size font loads:

```cpp
if (ImGui::SliderFloat("UI scale", &prefs.ui_scale, 0.8f, 2.0f, "%.2fx"))
    ImGui::GetStyle().FontScaleMain = prefs.ui_scale;   // live, all windows
```

Per-pane font sizes stay plain `SliderInt` + `PushFont(NULL, px)`. Theme
combo (dark/light) is a stored int — wired to `StyleColorsDark/Light` +
a style re-push at init in a real app.

## Verification recipe (what was actually run)

| # | Claim | Method | Result (2026-09-14) |
|---|-------|--------|---------------------|
| 1 | Builds zero warnings | `bash build.sh` (`-Wall -Wextra -Wpedantic -Wshadow`) | 0 warnings, binary 1.95 MB |
| 2 | Runs stable | `timeout 12 ./dreamide` | exit 124 (killed by timeout) — stable until then, correct for a GUI loop |
| 3 | Renders + layout correct | `capture_for_vision.sh` + vision tool | menu bar, toolbar, explorer, board, console all present; nothing off-window; long card titles truncate by design (clip rect) |

Note the layout decided at the source: `DrawBoard` replaces `DrawEditor` as
the center panel when the board is on, and the inspector starts hidden —
a vision check should assert against the panels *configured on*, not assume
a fixed set.

Stale-code audit note: do NOT add `ImGui::Combo\(` / `ImGui::ListBox\(`
patterns to the 1.92 stale-code audit grep — they false-positive. The old
helpers are current API: imgui.h:671 (Combo) and :795 (ListBox) keep them
"available for convenience" over the Begin*/End* forms. Both styles appear
in Panels.cpp (BeginCombo where the popup has custom contents, Combo for
plain int dropdowns); either is fine.
