// Panels.cpp — all UI for dreamIDE: single root window + modals.
#include "App.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include <cstdio>
#include <cstring>
#include <cctype>
#include <utility>

namespace ide {

static float ImClampf(float v, float mn, float mx) { return v < mn ? mn : (v > mx ? mx : v); }
static float ImMaxf(float a, float b) { return a > b ? a : b; }

// -------------------------------------------------------------------------
// UI-only state (file scope): modals, rename flow, palette navigation.
// -------------------------------------------------------------------------
static bool g_open_palette = false;
static bool g_open_prefs = false;
static bool g_open_about = false;
static std::string g_palette_filter;
static int  g_palette_index = 0;        // position within filtered list
static char g_rename_buf[128] = "";
static int  g_rename_id = -1;
static int  g_pending_close_doc = -1;   // file_id of tab closed this frame
static std::vector<Action> g_deferred;  // mutations queued while iterating containers

static void Defer(const Action& a) { g_deferred.push_back(a); }

void RequestCommandPalette() { g_open_palette = true; g_palette_filter.clear(); g_palette_index = 0; }
void RequestPreferences()    { g_open_prefs = true; }
void RequestAbout()          { g_open_about = true; }

void HandleEscape()
{
    if (g_open_palette) { g_open_palette = false; return; }
    if (g_open_prefs)   { g_open_prefs = false; return; }
    if (g_open_about)   { g_open_about = false; return; }
}

static void DrawToolbar(App& app);
static float DrawMenuBar(App& app);
static void DrawExplorer(App& app, float w);
static void DrawEditor(App& app);
static void DrawInspector(App& app, float w);
static void DrawConsole(App& app, float h);
static void DrawBoard(App& app);
static void DrawPalette(App& app);
static void DrawPrefs(App& app);
static void DrawAbout();
static void DrawRenameModal(App& app);
static void DrawExplorerChildren(App& app, int parent_id);

// -------------------------------------------------------------------------
void App::Draw()
{
    // Auto-save (when enabled): flush dirty docs as soon as they get dirty.
    if (prefs.autosave && project_dirty) DoSave();

    if (g_open_palette) { ImGui::OpenPopup("##palette"); g_open_palette = false; }
    if (g_open_prefs)   { ImGui::OpenPopup("##prefs");   g_open_prefs = false; }
    if (g_open_about)   { ImGui::OpenPopup("##about");   g_open_about = false; }
    if (g_rename_id >= 0) { ImGui::OpenPopup("##rename_modal"); }   // consumed by DrawRenameModal

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);

    const ImGuiWindowFlags root_flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##main", nullptr, root_flags);
    ImGui::PopStyleVar();

    DrawMenuBar(*this);
    DrawToolbar(*this);

    // ---- vertical split: top content vs bottom console ----
    const float avail_h = ImGui::GetContentRegionAvail().y;
    const float spacing = ImGui::GetStyle().ItemSpacing.y;
    float bottom_h = 0.0f;
    if (panels.console)
        bottom_h = ImClampf(ImGui::GetTextLineHeightWithSpacing() * 7.0f, 80.0f, avail_h * 0.45f);

    const float content_h = panels.console ? avail_h - bottom_h - spacing : avail_h;
    const float content_w = ImGui::GetContentRegionAvail().x;
    const float left_w  = panels.explorer  ? ImClampf(content_w * 0.21f, 150.0f, 340.0f) : 0.0f;
    const float right_w = panels.inspector ? ImClampf(content_w * 0.25f, 190.0f, 400.0f) : 0.0f;

    ImGui::BeginChild("##toprow", ImVec2(content_w, content_h), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (panels.explorer) DrawExplorer(*this, left_w);
    if (panels.explorer) ImGui::SameLine();
    if (panels.board) DrawBoard(*this); else DrawEditor(*this);
    if (panels.inspector) { ImGui::SameLine(); DrawInspector(*this, right_w); }
    ImGui::EndChild();

    if (panels.console) DrawConsole(*this, bottom_h);

    DrawPalette(*this);
    DrawPrefs(*this);
    DrawAbout();
    DrawRenameModal(*this);

    // flush deferred mutations now that no container iteration holds references
    for (const Action& a : g_deferred) Apply(a);
    g_deferred.clear();

    // deferred tab close (after tab bar iteration is done)
    if (g_pending_close_doc >= 0) { Apply({Action::CloseDoc, 0, g_pending_close_doc}); g_pending_close_doc = -1; }

    ImGui::End();
}

// -------------------------------------------------------------------------
static float DrawMenuBar(App& app)
{
    if (!ImGui::BeginMenuBar()) return 0.0f;

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("New Project", "Ctrl+N")) app.Apply({Action::NewProject});
        if (ImGui::MenuItem("Open Project", nullptr)) app.Apply({Action::OpenProject});
        ImGui::Separator();
        if (ImGui::MenuItem("Save", "Ctrl+S"))      app.Apply({Action::Save});
        if (ImGui::MenuItem("Save As...", nullptr)) app.Apply({Action::SaveAs, 0, 0, 0, 0, "Project Copy"});
        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Alt+F4"))      app.Apply({Action::Exit});
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View"))
    {
        ImGui::MenuItem("Project Explorer", nullptr, &app.panels.explorer);
        ImGui::MenuItem("Task Board", nullptr, &app.panels.board);
        ImGui::MenuItem("Output Console", nullptr, &app.panels.console);
        ImGui::MenuItem("Inspector", nullptr, &app.panels.inspector);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Tools"))
    {
        if (ImGui::MenuItem("Command Palette", "Ctrl+P")) RequestCommandPalette();
        if (ImGui::MenuItem("Preferences", "Ctrl+,"))     RequestPreferences();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help"))
    {
        if (ImGui::MenuItem("About dreamIDE")) RequestAbout();
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
    return ImGui::GetFrameHeight();
}

static void DrawToolbar(App& app)
{
    if (ImGui::Button("New"))   app.Apply({Action::NewProject});
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("New project (Ctrl+N)");
    ImGui::SameLine();
    if (ImGui::Button("Save"))  app.Apply({Action::Save});
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Save all documents (Ctrl+S)");
    ImGui::SameLine();
    if (ImGui::Button("Build")) app.Apply({Action::Build});
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Build the project (Ctrl+B)");
    ImGui::SameLine();
    if (ImGui::Button("Run"))   app.Apply({Action::Run});
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Run the project (F5)");

    ImGui::SameLine();
    ImGui::SetNextItemWidth(190);
    ImGui::InputTextWithHint("##tbsearch", "Search files and tasks...", &app.toolbar_search);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Filters the Explorer and Task Board");

    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    if (ImGui::BeginCombo("##config", app.project.config.c_str()))
    {
        for (const char* c : {"Debug", "Release", "RelWithDebInfo", "MinSizeRel"})
            if (ImGui::Selectable(c, app.project.config == c))
                app.Apply({Action::Config, 0, 0, 0, 0, c});
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Build configuration");

    // unsaved-changes indicator, right-aligned
    const char* dirty = app.project_dirty ? "* unsaved changes" : "all changes saved";
    const float w = ImGui::CalcTextSize(dirty).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - w);
    if (app.project_dirty) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", dirty);
    else                   ImGui::TextDisabled("%s", dirty);

    ImGui::Separator();
}

// -------------------------------------------------------------------------
// Project Explorer
// -------------------------------------------------------------------------
static void DrawExplorerRow(App& app, int idx)
{
    FileNode& n = app.project.files[idx];
    ImGui::PushID(n.id);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_OpenOnArrow;
    if (!n.is_folder) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (app.selection.kind == 1 && app.selection.file_id == n.id)
        flags |= ImGuiTreeNodeFlags_Selected;

    const char* icon = n.is_folder ? "[+] " : "[ ] ";
    const bool open = ImGui::TreeNodeEx(icon, flags, "%s", n.name.c_str()) && n.is_folder;

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        Defer({Action::SelectFile, 0, n.id});

    // double-click opens the file in a tab
    if (!n.is_folder && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    {
        int found = -1;
        for (int i = 0; i < (int)app.docs.size(); ++i)
            if (app.docs[i].file_id == n.id) found = i;
        if (found < 0) { Defer({Action::OpenDoc, 0, n.id}); found = (int)app.docs.size() - 1; }
        app.active_doc = found;
        Defer({Action::SelectFile, 0, n.id});
    }

    if (ImGui::BeginPopupContextItem("##ctx"))
    {
        if (ImGui::MenuItem("Rename...")) { g_rename_id = n.id; snprintf(g_rename_buf, sizeof(g_rename_buf), "%s", n.name.c_str()); ImGui::CloseCurrentPopup(); }
        if (ImGui::MenuItem("Duplicate")) Defer({Action::Duplicate, 0, n.id});
        if (ImGui::MenuItem("Delete"))    Defer({Action::Delete, 0, n.id});
        if (n.is_folder)
        {
            ImGui::Separator();
            if (ImGui::MenuItem("New File...")) Defer({Action::NewFile, 0, n.id, 0, 0, "untitled.txt"});
        }
        ImGui::EndPopup();
    }

    if (open) { DrawExplorerChildren(app, n.id); ImGui::TreePop(); }
    ImGui::PopID();
}

static void DrawExplorerChildren(App& app, int parent_id)
{
    for (int i = 0; i < (int)app.project.files.size(); ++i)
    {
        const FileNode& n = app.project.files[i];
        if (n.parent_id != parent_id) continue;
        DrawExplorerRow(app, i);
    }
}

static void DrawExplorer(App& app, float w)
{
    ImGui::BeginChild("##explorer", ImVec2(w, -FLT_MIN), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("EXPLORER");
    if (ImGui::BeginChild("##explorer_tree", ImVec2(0, -FLT_MIN), ImGuiChildFlags_None))
    {
        if (app.project.files.empty()) ImGui::TextDisabled("Empty project.");
        else DrawExplorerChildren(app, -1);
    }
    ImGui::EndChild();
    ImGui::EndChild();
}

// -------------------------------------------------------------------------
// Editor
// -------------------------------------------------------------------------
static void DrawEditor(App& app)
{
    ImGui::BeginChild("##editor", ImVec2(-FLT_MIN, 0), ImGuiChildFlags_Borders);

    if (app.docs.empty() || app.active_doc < 0 || app.active_doc >= (int)app.docs.size())
    {
        ImGui::TextDisabled("No document open.");
        ImGui::TextWrapped("Double-click a file in the Project Explorer, or create one via its right-click menu.");
        if (app.prefs.show_debug)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 rmin = ImGui::GetWindowPos();
            const ImVec2 rmax = ImVec2(rmin.x + ImGui::GetWindowWidth(), rmin.y + ImGui::GetWindowHeight());
            for (float x = rmin.x; x < rmax.x; x += 32.0f) dl->AddLine(ImVec2(x, rmin.y), ImVec2(x, rmax.y), 0x18FFFFFF);
            for (float y = rmin.y; y < rmax.y; y += 32.0f) dl->AddLine(ImVec2(rmin.x, y), ImVec2(rmax.x, y), 0x18FFFFFF);
        }
        ImGui::EndChild();
        return;
    }

    EditorDoc& doc = app.docs[app.active_doc];

    // font size preference applies to the editor area
    const float font_px = (float)app.prefs.editor_font;

    if (ImGui::BeginTabBar("##doctabs", ImGuiTabBarFlags_None))
    {
        const int count = (int)app.docs.size();     // cache: tabs closed via flag
        for (int i = 0; i < count; ++i)
        {
            EditorDoc& d = app.docs[i];
            ImGui::PushID(d.file_id);
            ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;
            if (d.dirty) flags |= ImGuiTabItemFlags_UnsavedDocument;
            bool opened = true;
            if (ImGui::BeginTabItem(d.title.c_str(), &opened, flags))
            {
                app.active_doc = i;
                ImGui::EndTabItem();
            }
            if (!opened) g_pending_close_doc = d.file_id;
            ImGui::PopID();
        }
        ImGui::EndTabBar();
    }

    // Find bar
    ImGui::SetNextItemWidth(230);
    ImGui::InputTextWithHint("##find", "Find...", &app.find_text);
    int matches = 0;
    if (!app.find_text.empty())
    {
        const std::string& hay = doc.text;
        const std::string& needle = app.find_text;
        for (size_t pos = hay.find(needle); pos != std::string::npos; pos = hay.find(needle, pos + needle.size()))
            ++matches;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%d match%s", matches, matches == 1 ? "" : "es");

    // editor body
    ImGui::PushFont(NULL, font_px);
    const float status_h = ImGui::GetFrameHeightWithSpacing();
    const float ed_h = ImGui::GetContentRegionAvail().y - status_h;
    ImGui::InputTextMultiline("##code", &doc.text, ImVec2(-FLT_MIN, ImMaxf(ed_h, ImGui::GetFrameHeight() * 4.0f)),
                              ImGuiInputTextFlags_AllowTabInput);
    if (ImGui::IsItemEdited()) doc.dirty = true;
    ImGui::PopFont();

    // status row
    int lines = 1;
    for (char c : doc.text) if (c == '\n') ++lines;
    ImGui::TextUnformatted(doc.dirty ? "(unsaved)" : "saved");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 240.0f);
    ImGui::TextDisabled("%d lines, %d chars", lines, (int)doc.text.size());

    ImGui::EndChild();
}

// -------------------------------------------------------------------------
// Inspector
// -------------------------------------------------------------------------
static std::string PathOf(const App& app, const FileNode& f)
{
    std::string path = f.name;
    int p = f.parent_id;
    while (p >= 0)
    {
        const FileNode* pf = app.FindFile(p);
        if (!pf) break;
        path = pf->name + "/" + path;
        p = pf->parent_id;
    }
    return "/" + path;
}

static void DrawInspector(App& app, float w)
{
    ImGui::BeginChild("##inspector", ImVec2(w, -FLT_MIN), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("INSPECTOR");

    if (app.selection.kind == 1)
    {
        FileNode* f = app.FindFile(app.selection.file_id);
        if (!f) { ImGui::TextDisabled("Selection no longer exists."); ImGui::EndChild(); return; }

        if (ImGui::BeginTable("##props", 2, ImGuiTableFlags_None))
        {
            ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 82.0f);
            ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
            char buf[256];

            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted("Name");
            ImGui::TableNextColumn(); snprintf(buf, sizeof(buf), "%s", f->name.c_str());
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("##iname", buf, sizeof(buf)))
                if (FileNode* fw = app.FindFile(f->id)) { fw->name = buf; app.project_dirty = true; }

            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted("Type");
            ImGui::TableNextColumn(); ImGui::TextUnformatted(f->is_folder ? "Folder" : "Source file");

            if (!f->is_folder)
            {
                ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted("Size");
                ImGui::TableNextColumn(); ImGui::Text("%d bytes", f->size_bytes);
            }
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted("Path");
            ImGui::TableNextColumn(); ImGui::TextWrapped("%s", PathOf(app, *f).c_str());
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted("Modified");
            ImGui::TableNextColumn(); ImGui::TextUnformatted(f->last_modified.c_str());
            ImGui::EndTable();
        }

        if (!f->is_folder && ImGui::Checkbox("Read-only", &f->read_only))
            app.project_dirty = true;

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Actions", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::Button("Duplicate")) Defer({Action::Duplicate, 0, f->id});
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
            if (ImGui::Button("Delete")) Defer({Action::Delete, 0, f->id});
            ImGui::PopStyleColor();
        }
    }
    else if (app.selection.kind == 2)
    {
        Task* t = nullptr;
        for (Task& x : app.tasks) if (x.id == app.selection.task_id) t = &x;
        if (!t) { ImGui::TextDisabled("Task no longer exists."); ImGui::EndChild(); return; }

        char tbuf[128]; snprintf(tbuf, sizeof(tbuf), "%s", t->title.c_str());
        if (ImGui::InputText("Title", tbuf, sizeof(tbuf))) t->title = tbuf;

        char dbuf[512]; snprintf(dbuf, sizeof(dbuf), "%s", t->description.c_str());
        if (ImGui::InputTextMultiline("Description", dbuf, sizeof(dbuf), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 3.0f)))
            t->description = dbuf;

        int prio = (int)t->priority;
        if (ImGui::Combo("Priority", &prio, "Low\0Medium\0High\0Critical\0")) t->priority = (Priority)prio;

        char abuf[64]; snprintf(abuf, sizeof(abuf), "%s", t->assignee.c_str());
        if (ImGui::InputText("Assignee", abuf, sizeof(abuf))) t->assignee = abuf;

        int status = (int)t->status;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::SliderInt("##status", &status, 0, 3, kStatusNames[status])) t->status = (TaskStatus)status;

        ImGui::SliderFloat("Progress", &t->progress, 0.0f, 1.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
    }
    else
    {
        ImGui::TextDisabled("Nothing selected.");
        ImGui::TextWrapped("Click a file in the Explorer or a task on the Board to inspect it.");
    }

    ImGui::EndChild();
}

// -------------------------------------------------------------------------
// Output console
// -------------------------------------------------------------------------
static void DrawConsole(App& app, float h)
{
    ImGui::BeginChild("##console", ImVec2(-FLT_MIN, h), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("OUTPUT");

    ImGui::Checkbox("Info", &app.console.show[0]); ImGui::SameLine();
    ImGui::Checkbox("Warning", &app.console.show[1]); ImGui::SameLine();
    ImGui::Checkbox("Error", &app.console.show[2]); ImGui::SameLine();
    ImGui::Checkbox("Success", &app.console.show[3]); ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &app.console.auto_scroll); ImGui::SameLine();
    if (ImGui::Button("Clear")) app.console.entries.clear();

    if (ImGui::BeginChild("##log", ImVec2(-FLT_MIN, -FLT_MIN), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_HorizontalScrollbar))
    {
        if (app.console.entries.empty())
            ImGui::TextDisabled("(console is empty)");
        else
        {
            const float font_px = (float)app.prefs.console_font;
            ImGui::PushFont(NULL, font_px);
            for (const LogEntry& e : app.console.entries)
            {
                if (!app.console.show[(int)e.sev]) continue;
                switch (e.sev)
                {
                case Severity::Info:    ImGui::Text("[INFO] %s", e.text.c_str()); break;
                case Severity::Warning: ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "[WARN] %s", e.text.c_str()); break;
                case Severity::Error:   ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "[ERROR] %s", e.text.c_str()); break;
                case Severity::Success: ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "[OK] %s", e.text.c_str()); break;
                }
            }
            if (app.console.auto_scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
                ImGui::SetScrollHereY(1.0f);
            ImGui::PopFont();
        }
    }
    ImGui::EndChild();
    ImGui::EndChild();
}

// -------------------------------------------------------------------------
// Task board: 4 equal columns; cards = bordered children with overlay text.
// -------------------------------------------------------------------------
static void DrawBoardCard(App& app, Task& t)
{
    const float line_h = ImGui::GetTextLineHeightWithSpacing();
    const float card_h = line_h * 2.0f + ImGui::GetStyle().FramePadding.y * 2.0f + 8.0f;

    ImGui::PushID(t.id);
    const ImVec2 card_pos = ImGui::GetCursorScreenPos();
    const float card_w = ImGui::GetContentRegionAvail().x;

    ImGui::BeginChild("##card", ImVec2(-FLT_MIN, card_h), ImGuiChildFlags_Borders);
    const bool selected = (app.selection.kind == 2 && app.selection.task_id == t.id);
    ImGui::Selectable("##sel", selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(card_w - 2.0f, card_h - 4.0f));
    if (ImGui::IsItemClicked()) Defer({Action::SelectTask, 0, t.id});
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        Defer({Action::MoveTask, 0, t.id, ((int)t.status + 1) % 4});
    if (ImGui::BeginPopupContextItem("##taskctx"))
    {
        for (int c = 0; c < 4; ++c)
            if (ImGui::MenuItem(kStatusNames[c], nullptr, (int)t.status == c))
                Defer({Action::MoveTask, 0, t.id, c});
        ImGui::Separator();
        if (ImGui::MenuItem("Delete Task"))
        {
            for (size_t i = 0; i < app.tasks.size(); ++i)
                if (app.tasks[i].id == t.id) { Defer({Action::DeleteTask, 0, t.id}); break; }
            if (app.selection.kind == 2 && app.selection.task_id == t.id) app.selection = { 0, -1, -1 };
        }
        ImGui::EndPopup();
    }
    // overlay text
    ImGui::SetCursorScreenPos(ImVec2(card_pos.x + 6.0f, card_pos.y + 3.0f));
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.25f, 0.65f, 0.35f, 1.0f));
    ImGui::ProgressBar(t.progress, ImVec2(card_w - 14.0f, 6.0f), "");
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos(ImVec2(card_pos.x + 6.0f, card_pos.y + 12.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
    ImGui::PushClipRect(ImVec2(card_pos.x + 1.0f, card_pos.y), ImVec2(card_pos.x + card_w - 1.0f, card_pos.y + card_h), true);
    ImGui::TextUnformatted(t.title.c_str());
    ImGui::TextDisabled("%s - %s", t.assignee.c_str(), kPriorityNames[(int)t.priority]);
    ImGui::PopClipRect();
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopID();
}

static void DrawBoard(App& app)
{
    ImGui::BeginChild("##board", ImVec2(-FLT_MIN, 0), ImGuiChildFlags_Borders);
    ImGui::SeparatorText("TASK BOARD");

    if (ImGui::Button("+ Add Task")) app.Apply({Action::AddTask, 0, 0, 0, 0, "New Task"});
    ImGui::SameLine();
    ImGui::TextDisabled("%d tasks  (double-click a card to advance it)", (int)app.tasks.size());

    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float col_w = (ImGui::GetContentRegionAvail().x - 3.0f * spacing) / 4.0f;

    for (int col = 0; col < 4; ++col)
    {
        ImGui::PushID(col);
        ImGui::BeginChild("##col", ImVec2(col_w, -FLT_MIN), ImGuiChildFlags_Borders);
        ImGui::SeparatorText(kStatusNames[col]);
        for (Task& t : app.tasks)
            if ((int)t.status == col) DrawBoardCard(app, t);
        ImGui::EndChild();
        if (col < 3) ImGui::SameLine();
        ImGui::PopID();
    }
    ImGui::EndChild();
}

// -------------------------------------------------------------------------
// Command palette
// -------------------------------------------------------------------------
static bool ContainsCI(const char* hay, const char* needle)
{
    if (!*needle) return true;
    for (const char* h = hay; *h; ++h)
    {
        const char* a = h; const char* b = needle;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { ++a; ++b; }
        if (!*b) return true;
    }
    return false;
}

static void DrawPalette(App& app)
{
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->WorkPos.x + ImGui::GetMainViewport()->WorkSize.x * 0.5f,
                                   ImGui::GetMainViewport()->WorkPos.y + ImGui::GetMainViewport()->WorkSize.y * 0.25f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(540, 0));

    if (!ImGui::BeginPopupModal("##palette", nullptr,
                                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint("##pfilter", "Type a command...", &g_palette_filter);

    // filter against command names
    int shown[16]; int n = 0;
    for (int i = 0; i < CommandCount() && n < 16; ++i)
        if (ContainsCI(CommandName(i), g_palette_filter.c_str()))
            shown[n++] = i;
    if (g_palette_index >= n) g_palette_index = n > 0 ? n - 1 : 0;

    ImGui::Separator();
    if (n == 0) ImGui::TextDisabled("(no matching command)");
    for (int s = 0; s < n; ++s)
    {
        const int ci = shown[s];
        char label[160];
        const char* sc = CommandShortcut(ci);
        if (sc && sc[0]) snprintf(label, sizeof(label), "%s   (%s)##cmd%d", CommandName(ci), sc, ci);
        else             snprintf(label, sizeof(label), "%s##cmd%d", CommandName(ci), ci);
        if (ImGui::Selectable(label, s == g_palette_index))
        {
            app.ExecuteCommand(ci);
            g_palette_filter.clear();
            ImGui::CloseCurrentPopup();
        }
        if (s == g_palette_index && ImGui::IsItemHovered()) {}
    }

    // keyboard navigation (Enter/arrows; InputText doesn't consume them)
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) g_palette_index = (g_palette_index + 1) % (n > 0 ? n : 1);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))   g_palette_index = (g_palette_index - 1 + n) % (n > 0 ? n : 1);
    if (n > 0 && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)))
    {
        app.ExecuteCommand(shown[g_palette_index]);
        g_palette_filter.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::Separator();
    ImGui::TextDisabled("Enter to run, arrows to move, Esc to close");
    ImGui::EndPopup();
}

// -------------------------------------------------------------------------
// Preferences
// -------------------------------------------------------------------------
static void DrawPrefs(App& app)
{
    ImGui::SetNextWindowSize(ImVec2(440, 0));
    if (!ImGui::BeginPopupModal("##prefs", nullptr,
                                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::SeparatorText("Preferences");

    if (ImGui::SliderFloat("UI scale", &app.prefs.ui_scale, 0.8f, 2.0f, "%.2fx"))
        ImGui::GetStyle().FontScaleMain = app.prefs.ui_scale;

    ImGui::Checkbox("Show debug grid (empty editor)", &app.prefs.show_debug);
    ImGui::Checkbox("Auto-save documents", &app.prefs.autosave);

    ImGui::Combo("Theme", &app.prefs.theme, "Dark\0Light\0");

    ImGui::SliderInt("Editor font", &app.prefs.editor_font, 10, 28, "%d px");
    ImGui::SliderInt("Console font", &app.prefs.console_font, 10, 28, "%d px");

    ImGui::Separator();
    ImGui::InputText("Project name", &app.project.name);

    ImGui::Separator();
    if (ImGui::Button("Close", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// -------------------------------------------------------------------------
// About
// -------------------------------------------------------------------------
static void DrawAbout()
{
    ImGui::SetNextWindowSize(ImVec2(420, 0));
    if (!ImGui::BeginPopupModal("##about", nullptr,
                                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::Text("dreamIDE - Project Desk");
    ImGui::TextDisabled("Version 0.1.0");
    ImGui::Separator();
    ImGui::TextWrapped("A small developer/project management workspace: project files, an editable code view, a Kanban task board, build console and preferences - all in one window, all in memory.");
    ImGui::Spacing();
    ImGui::TextWrapped("Built with Dear ImGui %s by Omar Cornut and contributors.", ImGui::GetVersion());
    ImGui::Spacing();
    if (ImGui::Button("Close", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// -------------------------------------------------------------------------
// Rename modal
// -------------------------------------------------------------------------
static void DrawRenameModal(App& app)
{
    if (g_rename_id < 0) return;   // nothing pending

    if (!ImGui::BeginPopupModal("##rename_modal", nullptr,
                                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize))
        return;

    const FileNode* f = app.FindFile(g_rename_id);
    ImGui::Text("Rename '%s'", f ? f->name.c_str() : "?");
    ImGui::SetNextItemWidth(280);
    bool done = ImGui::InputText("##newname", g_rename_buf, IM_COUNTOF(g_rename_buf),
                                 ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::Button("OK", ImVec2(80, 0))) done = true;
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(80, 0)))
    {
        g_rename_id = -1;
        ImGui::CloseCurrentPopup();
    }

    if (done)
    {
        if (g_rename_buf[0]) app.Apply({Action::Rename, 0, g_rename_id, 0, 0, g_rename_buf});
        g_rename_id = -1;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace ide
