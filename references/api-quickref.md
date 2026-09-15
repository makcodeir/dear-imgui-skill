# Dear ImGui API Quick Reference (1.92+ / 1.93)

Exact names and signatures for `IMGUI_VERSION_NUM >= 19198`. Everything here is
public API from `imgui.h`. Colour/flag enums are named exactly as written.

## Contents
1. Container pairing matrix
2. Window / child API
3. Table API
4. Tabs, menus, popups
5. Item & layout helpers
6. Style, colour, fonts
7. Key enums
8. Draw-list (custom rendering)

---

## 1. Container pairing matrix

| Begin                               | End               | End only if Begin returned true? |
|-------------------------------------|-------------------|----------------------------------|
| `Begin()`                           | `End()`           | **No — always call End**         |
| `BeginChild()`                      | `EndChild()`      | **No — always call EndChild**    |
| `BeginGroup()`                      | `EndGroup()`      | No — always call End             |
| `BeginDisabled()`                   | `EndDisabled()`   | No — always call End             |
| `BeginMultiSelect()`                | `EndMultiSelect()`| No — always call End             |
| `BeginPopup()` / `BeginPopupModal()`| `EndPopup()`      | Yes                              |
| `BeginTable()`                      | `EndTable()`      | Yes                              |
| `BeginCombo()`                      | `EndCombo()`      | Yes                              |
| `BeginListBox()`                    | `EndListBox()`    | Yes                              |
| `BeginTabBar()`                     | `EndTabBar()`     | Yes                              |
| `BeginTabItem()`                    | `EndTabItem()`    | Yes                              |
| `BeginMenuBar()`                    | `EndMenuBar()`    | Yes                              |
| `BeginMenu()`                       | `EndMenu()`       | Yes                              |
| `BeginTooltip()` / `BeginItemTooltip()` | `EndTooltip()`| Yes                            |
| `BeginMainMenuBar()`                | `EndMainMenuBar()`| Yes                              |
| `TreeNode()`                        | `TreePop()`       | Yes                              |
| `CollapsingHeader()`                | *(none)*          | — never needs a pop              |
| `BeginDragDropSource()`             | `EndDragDropSource()` | Yes                          |
| `BeginDragDropTarget()`             | `EndDragDropTarget()` | Yes                          |

> **Rule of thumb — `Begin()` and `BeginChild()` are the two documented odd ones
> out.** They are the *only* pair that must always be closed, even when the
> Begin returned false. (Everything else — except the void/pointer-returning
> `BeginGroup`/`BeginDisabled`/`BeginMultiSelect`, which unconditionally pair —
> must only be closed when it returned true.)

This is not folklore; `imgui.h` says it verbatim about `BeginChild`:

> *"Always call a matching EndChild() for each BeginChild() call, regardless of
> its return value. [Important: due to legacy reason, Begin/End and
> BeginChild/EndChild are inconsistent with all other functions such as
> BeginMenu/EndMenu, BeginPopup/EndPopup, etc. where the EndXXX call should only
> be called if the corresponding BeginXXX function returned true. Begin and
> BeginChild are the only odd ones out...]"*

Empirically verified (see `references/version-1.92-changes.md` §Verification
log): skipping `EndChild()` after a false `BeginChild()` asserts
`"Must call EndChild() and not End()!"` in `imgui.cpp`.

## 2. Window / child API

```cpp
bool Begin(const char* name, bool* p_open = NULL, ImGuiWindowFlags flags = 0);
void End();
bool BeginChild(const char* str_id, const ImVec2& size = ImVec2(0,0),
                ImGuiChildFlags child_flags = 0, ImGuiWindowFlags window_flags = 0);
bool BeginChild(ImGuiID id, const ImVec2& size = ImVec2(0,0),
                ImGuiChildFlags child_flags = 0, ImGuiWindowFlags window_flags = 0);
void EndChild();
```

`ImGuiChildFlags_`: `None`, `Borders` (=1, legacy `bool border=true`),
`AlwaysUseWindowPadding`, `ResizeX`, `ResizeY`, `AutoResizeX`, `AutoResizeY`,
`AlwaysAutoResize`, `FrameStyle`, `NavFlattened`.

`ImGuiWindowFlags_` (common): `None`, `NoTitleBar`, `NoResize`, `NoMove`,
`NoScrollbar`, `NoScrollWithMouse`, `NoCollapse`, `AlwaysAutoResize`,
`NoBackground`, `NoSavedSettings`, `NoMouseInputs`, `MenuBar`, `HorizontalScrollbar`,
`NoBringToFrontOnFocus`, `NoNavInputs`, `NoDecoration` (= NoTitleBar|NoResize|
NoScrollbar|NoCollapse), `NoInputs` (= NoMouseInputs|NoNavInputs|NoNavFocus).

Sizing helpers:
```cpp
void SetNextWindowSize(const ImVec2& size, ImGuiCond cond = 0);
void SetNextWindowPos(const ImVec2& pos, ImGuiCond cond = 0, const ImVec2& pivot = ImVec2(0,0));
void SetNextWindowSizeConstraints(const ImVec2& size_min, const ImVec2& size_max);
void SetNextWindowBgAlpha(float alpha);
```
`ImGuiCond_`: `None`, `Always`, `Once`, `FirstUseEver`, `Appearing`.
Use `FirstUseEver` for default sizes so user resizes persist (in `imgui.ini`).

Disable the ini file with `io.IniFilename = NULL;`.

## 3. Table API

```cpp
bool BeginTable(const char* str_id, int columns, ImGuiTableFlags flags = 0,
                const ImVec2& outer_size = ImVec2(0.0f,0.0f), float inner_width = 0.0f);
void EndTable();
void TableNextRow(ImGuiTableRowFlags row_flags = 0, float min_row_height = 0.0f);
bool TableNextColumn();
bool TableSetColumnIndex(int column_n);
void TableSetupColumn(const char* label, ImGuiTableColumnFlags flags = 0,
                      float init_width_or_weight = 0.0f, ImGuiID user_id = 0);
void TableSetupScrollFreeze(int cols, int rows);
void TableHeadersRow();
void TableHeader(const char* label);
```

`ImGuiTableFlags_` (common): `Resizable`, `Reorderable`, `Hideable`, `Sortable`,
`SizingFixedFit`, `SizingFixedSame`, `SizingStretchProp`, `SizingStretchSame`,
`NoPadOuterX`, `NoPadInnerX`, `ScrollX`, `ScrollY`, `RowBg`, `Borders`,
`BordersInner`, `BordersOuter`, `NoBordersInBody`, `BordersH`, `BordersV`,
`NoSavedSettings`, `ContextMenuInBody`.

`ImGuiTableColumnFlags_`: `None`, `WidthStretch`, `WidthFixed`, `DefaultSort`,
`WidthIndent`, `NoHeaderLabel`, `Disabled`, `NoResize`, `NoReorder`, `NoHide`,
`NoSort`, `NoSortAscending`, `NoSortDescending`.

Minimal table:
```cpp
if (ImGui::BeginTable("t", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
{
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Type");
    ImGui::TableSetupColumn("Value");
    ImGui::TableHeadersRow();
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("radius");
    ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted("float");
    ImGui::TableSetColumnIndex(2); ImGui::Text("%.2f", 3.0f);
    ImGui::EndTable();
}
```

## 4. Tabs, menus, popups

```cpp
bool BeginTabBar(const char* str_id, ImGuiTabBarFlags flags = 0);   void EndTabBar();
bool BeginTabItem(const char* label, bool* p_open = NULL, ImGuiTabItemFlags flags = 0); void EndTabItem();
bool BeginMenuBar(); void EndMenuBar();     // requires ImGuiWindowFlags_MenuBar on Begin()
bool BeginMainMenuBar(); void EndMainMenuBar();
bool BeginMenu(const char* label, bool enabled = true); void EndMenu();
bool MenuItem(const char* label, const char* shortcut = NULL, bool selected = false, bool enabled = true);
bool MenuItem(const char* label, const char* shortcut, bool* p_selected, bool enabled = true);

void OpenPopup(const char* str_id);        // call this to trigger on the next frame
bool BeginPopup(const char* str_id, ImGuiWindowFlags flags = 0);            void EndPopup();
bool BeginPopupModal(const char* name, bool* p_open = NULL, ImGuiWindowFlags flags = 0); void EndPopup();
void CloseCurrentPopup();
bool IsPopupOpen(const char* str_id, ImGuiPopupFlags flags = 0);            // ImGuiPopupFlags_AnyPopupId etc.
bool IsItemHovered(ImGuiHoveredFlags flags = 0);
void SetTooltip(const char* fmt, ...);
void BeginTooltip(); void EndTooltip();
```

Modal pattern:
```cpp
if (something_wants_confirm)
    ImGui::OpenPopup("Confirm");
if (ImGui::BeginPopupModal("Confirm", NULL, ImGuiWindowFlags_AlwaysAutoResize))
{
    ImGui::TextUnformatted("Are you sure?");
    ImGui::Separator();
    if (ImGui::Button("OK", ImVec2(120,0))) { ImGui::CloseCurrentPopup(); }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120,0))) { ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup();
}
```

## 5. Item & layout helpers

```cpp
bool IsItemHovered(ImGuiHoveredFlags flags = 0);
bool IsItemActive();
bool IsItemClicked(ImGuiMouseButton mouse_button = 0);
bool IsItemEdited();
void SetItemDefaultFocus();          // for combo/list: focus the selected entry
void SetKeyboardFocusHere(int offset = 0);
void SetNextItemOpen(bool is_open, ImGuiCond cond = 0);
void SetNextItemWidth(float item_width);
void PushItemWidth(float item_width); void PopItemWidth();
bool BeginItemTooltip();  void SetItemTooltip(const char* fmt, ...);

void SameLine(float offset_from_start_x = 0.0f, float spacing = -1.0f);
void NewLine();
void Spacing();
void Dummy(const ImVec2& size);
void Indent(float indent_w = 0.0f);  void Unindent(float indent_w = 0.0f);
void BeginGroup(); void EndGroup();
void BeginDisabled(bool disabled = true); void EndDisabled();
void PushTextWrapPos(float wrap_local_pos_x = 0.0f); void PopTextWrapPos();
void PushClipRect(...); void PopClipRect();  // advanced

ImVec2 GetContentRegionAvail();               // remaining space in current container
ImVec2 GetCursorPos(); ImVec2 GetCursorScreenPos();
void   SetCursorPos(const ImVec2& local_pos); void SetCursorScreenPos(const ImVec2& pos);
ImVec2 GetWindowPos(); ImVec2 GetWindowSize();
ImVec2 GetItemRectMin(); ImVec2 GetItemRectMax(); ImVec2 GetItemRectSize();
float  GetTextLineHeight(); float GetTextLineHeightWithSpacing();
float  GetFrameHeight(); float GetFrameHeightWithSpacing();
float  GetFontSize();                         // scaled — do NOT pass to PushFont()
float  GetTime(); int GetFrameCount();
```

## 6. Style, colour, fonts

```cpp
ImGuiStyle& style = ImGui::GetStyle();
style.ScaleAllSizes(float scale);             // call once at init
style.FontSizeBase  = 20.0f;                  // default font size (unscaled)
style.FontScaleDpi  = dpi_scale;              // global font scale
style.WindowPadding = ImVec2(8, 8);
style.FramePadding  = ImVec2(4, 3);
style.ItemSpacing   = ImVec2(8, 4);
style.ItemInnerSpacing = ImVec2(4, 4);
style.FrameRounding = 4.0f;
style.WindowRounding = 0.0f;
style.Alpha = 1.0f;
style.WindowBorderSize = 1.0f; style.FrameBorderSize = 0.0f;

ImGui::StyleColorsDark();  ImGui::StyleColorsLight();  ImGui::StyleColorsClassic();

void PushStyleColor(ImGuiCol idx, ImU32 col);
void PushStyleColor(ImGuiCol idx, const ImVec4& col);
void PopStyleColor(int count = 1);
void PushStyleVar(ImGuiStyleVar idx, float val);
void PushStyleVar(ImGuiStyleVar idx, const ImVec2& val);
void PushStyleVarX(ImGuiStyleVar idx, float val_x);   // also PushStyleVarY
void PopStyleVar(int count = 1);
ImU32  GetColorU32(ImGuiCol idx, float alpha_mul = 1.0f);
ImU32  GetColorU32(const ImVec4& col);
ImVec4 GetStyleColorVec4(ImGuiCol idx);

// Fonts (1.92+ dynamic system: no glyph ranges, no size argument needed)
io.Fonts->AddFontDefault();               // auto-select embedded font
io.Fonts->AddFontDefaultVector();         // scalable embedded font
io.Fonts->AddFontDefaultBitmap();         // legacy pixel font
io.Fonts->AddFontFromFileTTF("font.ttf"); // no size needed in 1.92+
io.Fonts->AddFontFromFileTTF("font.ttf", 18.0f, &config);  // explicit size + config
ImFontConfig config; config.MergeMode = true;              // merge icon font
ImFont* f = io.Fonts->AddFontFromFileTTF("i.ttf", 13.0f, &config);
ImGui::PushFont(f, 0.0f);   // 0 = keep current size; NULL = keep current font
ImGui::PushFont(NULL, 32.0f);
ImGui::PopFont();
```

## 7. Key enums

```cpp
ImGuiMouseButton_Left = 0, _Right = 1, _Middle = 2;   // and ImGuiMouseButton_COUNT

ImGuiInputTextFlags_: None, CharsDecimal, CharsHexadecimal, CharsUppercase,
  CharsNoBlank, AutoSelectAll, EnterReturnsTrue, ReadOnly, Password, AllowTabInput,
  CtrlEnterForNewLine, NoUndoRedo, NoHorizontalScroll, AlwaysOverwrite,
  NoCursorBlink, Multiline (auto), NoMarkEdited.

ImGuiSliderFlags_: None, AlwaysClamp, Logarithmic, NoRoundToFormat,
  NoInput, WrapAround, ClampOnInput, ClampZeroRange.
ImGuiSelectableFlags_: None, DontClosePopups, SpanAllColumns, AllowDoubleClick,
  Disabled, AllowOverlap, SelectOnNav.

ImGuiKey_* : Tab, LeftArrow, RightArrow, UpArrow, DownArrow, PageUp, PageDown,
  Home, End, Insert, Delete, Backspace, Space, Enter, Escape, A..Z, 0..9,
  F1..F12, Keypad0..9, modifiers LeftShift/RightShift/LeftCtrl/RightCtrl/
  LeftAlt/RightAlt/LeftSuper/RightSuper, Gamepad*.
ImGuiMod_Ctrl, ImGuiMod_Shift, ImGuiMod_Alt, ImGuiMod_Super.

ImGuiCol_ (common): Text, TextDisabled, WindowBg, ChildBg, PopupBg, Border,
  FrameBg, FrameBgHovered, FrameBgActive, TitleBg, TitleBgActive, MenuBarBg,
  ScrollbarBg, ScrollbarGrab, CheckMark, SliderGrab, SliderGrabActive, Button,
  ButtonHovered, ButtonActive, Header, HeaderHovered, HeaderActive, Separator,
  ResizeGrip, Tab, TabHovered, TabActive, PlotLines, TextSelectedBg, TableHeaderBg,
  TableBorderStrong, TableBorderLight, TableRowBg, TableRowBgAlt, NavHighlight.

ImGuiStyleVar_: Alpha, DisabledAlpha, WindowPadding, WindowRounding,
  WindowBorderSize, WindowMinSize, WindowTitleAlign, ChildRounding, ChildBorderSize,
  PopupRounding, PopupBorderSize, FramePadding, FrameRounding, FrameBorderSize,
  ItemSpacing, ItemInnerSpacing, CellPadding, IndentSpacing, ScrollbarSize,
  ScrollbarRounding, GrabMinSize, GrabRounding, TabRounding, ButtonTextAlign,
  SelectableTextAlign, SeparatorTextBorderSize, SeparatorTextAlign,
  SeparatorTextPadding.
```

## 8. Draw-list (custom rendering)

```cpp
ImDrawList* dl = ImGui::GetWindowDrawList();     // or GetBackground/ForegroundDrawList()
ImVec2 p = ImGui::GetCursorScreenPos();
dl->AddCircleFilled(ImVec2(p.x+50, p.y+50), 30.0f, IM_COL32(255,0,0,255));
dl->AddLine(p, ImVec2(p.x+100, p.y+100), IM_COL32(255,255,0,255), 3.0f);
dl->AddRectFilled(p, ImVec2(p.x+200, p.y+60), IM_COL32(0,128,255,128), 6.0f);
dl->AddText(p, IM_COL32(255,255,255,255), "drawlist text");
ImGui::Dummy(ImVec2(200, 200));                  // claim space or the window shrinks
```
`GetBackgroundDrawList()` / `GetForegroundDrawList()` draw behind/over all
windows. Colour helper: `IM_COL32(r,g,b,a)` or `ImGui::GetColorU32(...)` (which
multiplies by `style.Alpha`).
