// Dear ImGui — minimal headless widget gallery (no windowing, no GPU).
// Compiles and runs with just the core library: no GLFW/SDL/OpenGL required.
//
// Why this file exists: it is the fastest way to verify that a widget-layout
// change compiles cleanly under -Wall -Wextra -Wpedantic before you wire it
// into a real backend. Run it in CI, in a container, or on a headless box.
//
// Build (from the Dear ImGui repository root):
//   g++ -std=c++11 -I. -Wall -Wextra -Wpedantic -Wshadow
//       -o /tmp/gallery gallery.cpp imgui.cpp imgui_demo.cpp imgui_draw.cpp
//       imgui_tables.cpp imgui_widgets.cpp
//
// Notes for 1.92+/1.93:
//   * IM_COUNTOF replaced IM_ARRAYSIZE (renamed in 1.92.6).
//   * PushFont() now requires an explicit size argument: PushFont(NULL, size).
//   * Glyph ranges are obsolete; AddFontFromFileTTF() works with no size argument.

#include "imgui.h"
#include <stdio.h>

// Persistent application state. In a real tool this usually lives in a struct
// owned by your app; we keep it file-static here to stay self-contained.
struct AppState
{
    bool  show_demo_window;
    bool  show_log;
    int   selected;
    int   model_index;
    float radius;
    bool  wireframe;
    bool  show_confirm;
    char  name[128];
    char  filter[64];
};

// A small reusable "tool window" following the canonical Dear ImGui layout:
// menu bar -> optional toolbar -> body. This is the shape almost every example
// in examples/ and every demo app in imgui_demo.cpp converges on.
static void DrawMenuBar(AppState& st)
{
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            ImGui::MenuItem("New", "Ctrl+N");
            ImGui::MenuItem("Open", "Ctrl+O");
            ImGui::Separator();
            if (ImGui::MenuItem("Close", "Ctrl+W"))
                st.show_confirm = true; // let the modal own the real close
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem("Demo window", NULL, &st.show_demo_window);
            ImGui::MenuItem("Log", NULL, &st.show_log);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
}

// Left pane: a scrollable list of items. The ## suffix keeps the visible label
// free of the numeric index while still giving each row a stable unique ID.
static void DrawLeftPane(AppState& st)
{
    ImGui::BeginChild("left pane", ImVec2(180.0f, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    ImGui::TextUnformatted("Objects");
    ImGui::Separator();
    for (int i = 0; i < 12; i++)
    {
        char label[64];
        snprintf(label, IM_COUNTOF(label), "Object %02d###obj%02d", i, i);
        if (ImGui::Selectable(label, st.selected == i))
            st.selected = i;
    }
    ImGui::EndChild();
}

// Right pane: property editor. Grouping + a table keeps labels and controls
// aligned without hardcoding pixel columns.
static void DrawProperties(AppState& st)
{
    ImGui::SeparatorText("Properties");

    if (ImGui::BeginTable("props", 2, ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("field", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextUnformatted("Name");
        ImGui::TableNextColumn();
        // width: -FLT_MIN makes the widget span the remaining cell width.
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##name", st.name, IM_COUNTOF(st.name));

        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextUnformatted("Radius");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderFloat("##radius", &st.radius, 0.0f, 10.0f, "%.2f");

        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextUnformatted("Wireframe");
        ImGui::TableNextColumn();
        ImGui::Checkbox("##wire", &st.wireframe);

        ImGui::EndTable();
    }

    // BeginCombo/EndCombo is preferred over the legacy Combo() API: it lets you
    // iterate any container yourself (see references/api-quickref.md).
    static const char* kModels[] = { "Cube", "Sphere", "Cylinder", "Torus" };
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::BeginCombo("Model", kModels[st.model_index]))
    {
        for (int n = 0; n < IM_COUNTOF(kModels); n++)
        {
            const bool is_selected = (st.model_index == n);
            if (ImGui::Selectable(kModels[n], is_selected))
                st.model_index = n;
            if (is_selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (ImGui::CollapsingHeader("Advanced"))
    {
        ImGui::Indent();
        ImGui::TextWrapped("Values expressed relative to the font size stay "
                           "correct across DPI and font changes.");
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * 0.5f));
        ImGui::Unindent();
    }
}

static void DrawLogWindow(AppState& st)
{
    if (!st.show_log)
        return;
    ImGui::SetNextWindowSize(ImVec2(420.0f, 200.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Log", &st.show_log))
    {
        ImGui::BeginChild("scrolling", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()),
                          ImGuiChildFlags_Borders);
        for (int i = 0; i < 40; i++)
            ImGui::Text("%04d: tick", i);
        ImGui::EndChild();
        if (ImGui::Button("Clear")) {}
        ImGui::SameLine();
        ImGui::TextDisabled("(%d entries)", 40);
    }
    ImGui::End();
}

// Modal confirmation. Always pair BeginPopupModal/EndPopup inside the same
// scope; only call EndPopup() when BeginPopupModal() returned true.
static void DrawConfirmModal(AppState& st)
{
    if (st.show_confirm)
        ImGui::OpenPopup("Confirm close");
    if (ImGui::BeginPopupModal("Confirm close", NULL,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Discard unsaved changes?");
        ImGui::Separator();
        if (ImGui::Button("Yes", ImVec2(120.0f, 0.0f)))
        {
            st.show_confirm = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(120.0f, 0.0f)))
        {
            st.show_confirm = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

static void DrawMainWindow(AppState& st)
{
    ImGui::SetNextWindowSize(ImVec2(680.0f, 440.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("ImGui GUI Construction Template", NULL, ImGuiWindowFlags_MenuBar))
    {
        DrawMenuBar(st);

        DrawLeftPane(st);
        ImGui::SameLine();

        ImGui::BeginGroup();
        DrawProperties(st);
        ImGui::EndGroup();
    }
    ImGui::End();
}

int main(int, char**)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = NULL; // headless: don't write imgui.ini
    // Headless smoke test: claim renderer texture support so the core library
    // does not assert that a renderer backend built/uploaded the font atlas.
    // A real app gets this flag from ImGui_ImplXxx_Init().
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    ImGui::StyleColorsDark();

    AppState st = {};
    st.radius = 3.0f;
    snprintf(st.name, IM_COUNTOF(st.name), "mesh_001");

    for (int frame = 0; frame < 3; frame++)
    {
        // Headless: a real backend fills these in its _NewFrame(). Without them
        // Dear ImGui asserts on an invalid DisplaySize during NewFrame().
        io.DisplaySize = ImVec2(1280.0f, 720.0f);
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        DrawMainWindow(st);
        DrawLogWindow(st);
        DrawConfirmModal(st);
        if (st.show_demo_window)
            ImGui::ShowDemoWindow(&st.show_demo_window);
        ImGui::Render();
    }

    printf("Rendered %d frames without error.\n", 3);
    ImGui::DestroyContext();
    return 0;
}
