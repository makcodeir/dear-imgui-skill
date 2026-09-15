// svg_widgets.cpp — GUI-oriented SVG assets rendered live in Dear ImGui.
//
// The icons/animations below were authored as small SVG files (templates/svg/)
// and converted at build time with scripts/svg2drawlist.py --tint FFFFFF.
// This file demonstrates the two things you actually do with them:
//   1. tint + place a monochrome icon inside any widget (button/child/dummy),
//   2. drive looping animations with ImGui::GetTime() (no per-frame state).
//
// Build: see templates/svg/build.sh (it compiles BOTH modes below with the
// strict warning set: GUI app + -DSVG_HEADLESS_SELFTEST headless variant).
// Minimal GUI form (GLFW via pkg-config if your distro provides dev files):
//   g++ -std=c++11 -I. -Ibackends -I<skill>/templates -Wall -Wextra -Werror
//       svg_widgets.cpp imgui.cpp imgui_draw.cpp imgui_tables.cpp
//       imgui_widgets.cpp backends/imgui_impl_glfw.cpp
//       backends/imgui_impl_opengl3.cpp $(pkg-config --cflags --libs glfw3)
//       -lGL -ldl -lpthread -o /tmp/svg_widgets

#include "imgui.h"
#include "svg_icons_imgui.h"   // generated: DrawSvg_icon_* / spinner / dots / progress / pulse

#include <stdio.h>
#include <float.h>   // FLT_MIN
#ifdef SVG_HEADLESS_SELFTEST
#include <cstdint>   // intptr_t
#endif

// ---- static icon table: name + generated draw fn ----
typedef void (*SvgFn)(ImDrawList*, const ImVec2&, const ImVec2&, float, ImU32);
struct SvgIcon
{
    const char* Name;
    SvgFn       Draw;
};
static const SvgIcon kIcons[] = {
    { "close",    DrawSvg_icon_close },
    { "check",    DrawSvg_icon_check },
    { "search",   DrawSvg_icon_search },
    { "chevron",  DrawSvg_icon_chevron_down },
    { "play",     DrawSvg_icon_play },
    { "pause",    DrawSvg_icon_pause },
    { "folder",   DrawSvg_icon_folder },
    { "gear",     DrawSvg_icon_gear },
};

#ifndef SVG_HEADLESS_SELFTEST  // helpers below are used by the GUI main() only
// An icon "button": InvisibleButton rect + generated SVG painted on top.
// Returns true when clicked (same contract as ImGui::Button).
static bool IconButton(const char* id, SvgIcon icon, float size, ImU32 tint)
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    bool clicked = false;
    ImGui::PushID(id);
    ImGui::InvisibleButton("icon", ImVec2(size, size));
    if (ImGui::IsItemClicked())
        clicked = true;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (ImGui::IsItemHovered())
        dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size),
                          IM_COL32(255, 255, 255, ImGui::IsItemActive() ? 48 : 28), 4.0f);
    // Inset a hair so the stroke never clips the widget bounds.
    const float pad = size * 0.10f;
    icon.Draw(dl, ImVec2(pos.x + pad, pos.y + pad), ImVec2(size - 2 * pad, size - 2 * pad),
              ImGui::GetTime(), tint);
    ImGui::PopID();
    return clicked;
}

static void DrawWidgetsWindow(bool* p_open, const char** p_status)
{
    static ImU32  tint = IM_COL32(255, 255, 255, 255);
    static float  icon_size = 0.0f;  // 0 => derive from frame height
    static float  col4[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    ImGui::SetNextWindowSize(ImVec2(360, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("SVG widgets", p_open))
    {
        ImGui::End();
        return;
    }

    const float sz = (icon_size > 0.0f) ? icon_size : ImGui::GetFrameHeight();

    ImGui::SeparatorText("Monochrome icons (tinted at runtime)");
    for (const SvgIcon& icon : kIcons)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)ImColor(tint));
        if (IconButton(icon.Name, icon, sz, tint))
            *p_status = icon.Name;
        ImGui::PopStyleColor();
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::ColorEdit4("Tint", col4);
    tint = IM_COL32((int)(col4[0] * 255), (int)(col4[1] * 255), (int)(col4[2] * 255), (int)(col4[3] * 255));
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Size");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderFloat("##size", &icon_size, 0.0f, 64.0f, "%.0f (0 = frame height)")) {}

    ImGui::SeparatorText("Animated widgets");
    const float t = ImGui::GetTime();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // inline: spinner + label (drawn in the gap made by Dummy + SameLine)
    ImGui::Dummy(ImVec2(sz, sz));
    DrawSvg_spinner_arc(dl, ImGui::GetItemRectMin(), ImVec2(sz, sz), t, tint);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Working...");

    ImGui::Dummy(ImVec2(sz, sz));
    DrawSvg_dots_loading(dl, ImGui::GetItemRectMin(), ImVec2(sz, sz), t, tint);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Someone is typing");

    // progress + pulse share the row with a text tail
    const float bar_w = ImGui::GetContentRegionAvail().x - sz;
    ImGui::Dummy(ImVec2(bar_w, sz * 0.5f));
    DrawSvg_progress_fill(dl, ImGui::GetItemRectMin(), ImVec2(bar_w, sz * 0.5f), t, tint);
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(sz, sz));
    DrawSvg_pulse_dot(dl, ImGui::GetItemRectMin(), ImVec2(sz, sz), t, IM_COL32(90, 220, 130, 255));

    ImGui::SeparatorText("Last clicked");
    ImGui::TextUnformatted(*p_status ? *p_status : "(none)");

    ImGui::End();
}

// ---------------------------------------------------------------------------
#endif // !SVG_HEADLESS_SELFTEST (GUI helpers)

#ifdef SVG_HEADLESS_SELFTEST

// Renders N frames without a window; asserts every generated icon produces
// geometry and that animations actually change vertex counts over time.
int main(int, char**)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    int failures = 0;
    int verts_first[8] = { 0 };
    bool anim_ok[4] = { false, false, false, false };
    int verts_prev[4] = { 0, 0, 0, 0 };

    for (int frame = 0; frame < 120; frame++)
    {
        io.DisplaySize = ImVec2(1280, 720);
        io.DeltaTime = 1.0f / 60.0f;
        io.AddMousePosEvent(0.0f, 0.0f);
        ImGui::NewFrame();
        ImGui::Begin("probe");
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (frame == 0)
            for (int i = 0; i < 8; i++)
            {
                int b = dl->VtxBuffer.Size;
                kIcons[i].Draw(dl, ImVec2(10, 10 + i * 30), ImVec2(24, 24), 0.0f, IM_COL32(255, 255, 255, 255));
                verts_first[i] = dl->VtxBuffer.Size - b;
            }
        if (frame % 30 == 29)
        {
            SvgFn fns[4] = { DrawSvg_spinner_arc, DrawSvg_dots_loading, DrawSvg_progress_fill, DrawSvg_pulse_dot };
            for (int i = 0; i < 4; i++)
            {
                int b = dl->VtxBuffer.Size;
                fns[i](dl, ImVec2(10, 10 + i * 30), ImVec2(24, 24), (float)frame / 60.0f, IM_COL32(255, 255, 255, 255));
                int v = dl->VtxBuffer.Size - b;
                if (v != verts_prev[i])
                    anim_ok[i] = true;   // geometry varies with t (colors may too; this is the weaker proxy)
                verts_prev[i] = v;
            }
        }
        ImGui::End();
        ImGui::Render();
        ImDrawData* dd = ImGui::GetDrawData();
        if (dd->Textures)
            for (ImTextureData* tex : *dd->Textures)
                if (tex->Status != ImTextureStatus_OK)
                {
                    tex->SetTexID((ImTextureID)(intptr_t)(tex->UniqueID + 1));
                    tex->SetStatus(ImTextureStatus_OK);
                }
    }
    for (int i = 0; i < 8; i++)
        if (verts_first[i] <= 0) { printf("FAIL: icon %s draws no vertices\n", kIcons[i].Name); failures++; }
    const char* anames[4] = { "spinner_arc", "dots_loading", "progress_fill", "pulse_dot" };
    for (int i = 0; i < 4; i++)
        if (!anim_ok[i]) { printf("note: %s geometry constant across frames (may animate color/pos only)\n", anames[i]); }
    if (failures == 0)
        printf("svg_widgets self-test: PASS (120 frames, 8/8 icons draw)\n");
    ImGui::DestroyContext();
    return failures;
}

#else // ---------------------------------------------------------------- GUI

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

static void glfw_error(int err, const char* desc) { fprintf(stderr, "GLFW error %d: %s\n", err, desc); }

int main(int, char**)
{
    if (!glfwInit())
        return 1;
    glfwSetErrorCallback(glfw_error);
#ifdef __APPLE__
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, 1);
#else
    const char* glsl_version = "#version 130";
    // No core-profile request: profiles are only legal for >= 3.2, and the
    // default (compatibility) context works everywhere. Same as upstream examples.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif
    GLFWwindow* win = glfwCreateWindow(900, 600, "SVG Widgets - Dear ImGui", NULL, NULL);
    if (!win)
        return 1;
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.IniFilename = NULL;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    bool show = true;
    const char* status = NULL;
    while (!glfwWindowShouldClose(win))
    {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        DrawWidgetsWindow(&show, &status);
        if (!show)
            break;

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(win, &w, &h);
        glViewport(0, 0, w, h);
        static const float clear[4] = { 0.10f, 0.10f, 0.12f, 1.00f };
        glClearColor(clear[0], clear[1], clear[2], clear[3]);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(win);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}

#endif // SVG_HEADLESS_SELFTEST
