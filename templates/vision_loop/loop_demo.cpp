// loop_demo.cpp - minimal Dear ImGui app wired to imgui_harness.h: the
// reference target for the automated visual verification loop.
//
// Build: bash build.sh   (strict warnings; links static GLFW from DBSCAN tree)
// Run:   ./loop_demo --control /tmp/loop_demo.sock   (then: see closed_loop.sh)
//
// Testable surface (every state change is visible in 3 places so the vision
// step is checkable): toolbar text "Count: N", big centered canvas text,
// canvas background colour (grey <-> blue), gauge bar at canvas bottom.

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_harness.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

// ---- app model (flat, plain) ----
static int   g_count = 0;
static float g_gauge = 0.50f;
static bool  g_blue  = false;

static void build_ui()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Vision Loop Demo", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    Harness::BeginActions(); // rects are rebuilt every frame from live layout

    // --- toolbar row ---
    ImGui::Text("Count: %d", g_count);
    ImGui::SameLine();
    if (ImGui::Button("Minus"))  g_count--;
    Harness::Action("dec");
    ImGui::SameLine();
    if (ImGui::Button("Plus"))   g_count++;
    Harness::Action("inc");
    ImGui::SameLine();
    ImGui::Checkbox("Blue canvas", &g_blue);
    Harness::Action("blue");
    ImGui::SameLine();
    if (ImGui::Button("Reset")) { g_count = 0; g_gauge = 0.50f; g_blue = false; }
    Harness::Action("reset");

    ImGui::SeparatorText("Controls");
    ImGui::SetNextItemWidth(320.0f);
    ImGui::SliderFloat("Gauge", &g_gauge, 0.0f, 1.0f, "%.2f");
    Harness::Action("gauge");

    // --- canvas (ImDrawList): the visual payload the screenshot must show ---
    const ImVec2 top = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 br(top.x + avail.x, top.y + avail.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(top, br, g_blue ? IM_COL32(30, 60, 140, 255)
                                      : IM_COL32(45, 45, 52, 255));
    char msg[64];
    snprintf(msg, sizeof(msg), "Count: %d", g_count);
    const ImVec2 ts = ImGui::CalcTextSize(msg);
    // font size 48 for a banner a vision model can read at 1024px width
    dl->AddText(nullptr, 48.0f,
                ImVec2((top.x + br.x - ts.x * 4.0f) * 0.5f, (top.y + br.y) * 0.5f - 48.0f),
                IM_COL32(255, 255, 255, 255), msg);
    // gauge bar along the bottom, width proportional
    const float bar_h = 24.0f, pad = 12.0f;
    dl->AddRectFilled(ImVec2(top.x + pad, br.y - bar_h - pad),
                      ImVec2(top.x + pad + (br.x - top.x - 2 * pad) * g_gauge, br.y - pad),
                      IM_COL32(80, 200, 120, 255));
    dl->AddRect(ImVec2(top.x + pad, br.y - bar_h - pad),
                ImVec2(br.x - pad, br.y - pad), IM_COL32(160, 160, 160, 255));
    Harness::ActionAt("canvas", (top.x + br.x) * 0.5f, (top.y + br.y) * 0.5f);

    ImGui::End();
}

int main(int argc, char** argv)
{
    char sockpath[256];
    snprintf(sockpath, sizeof(sockpath), "/tmp/imgui_harness_%d.sock", (int)getpid());
    for (int i = 1; i + 1 < argc; i++)
        if (strcmp(argv[i], "--control") == 0)
            snprintf(sockpath, sizeof(sockpath), "%s", argv[i + 1]);

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE); // stable captures
    GLFWwindow* window = glfwCreateWindow(760, 460, "Vision Loop Demo", nullptr, nullptr);
    if (!window)
        return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    if (!Harness::Init(sockpath))
        return 1;
    Harness::VarInt("count", &g_count);
    Harness::VarFloat("gauge", &g_gauge);
    Harness::VarBool("blue", &g_blue);
    printf("control socket: %s\n", sockpath);
    fflush(stdout);

    // --- frame loop: backend NF -> Harness::Poll -> NewFrame -> UI -> present
    while (!glfwWindowShouldClose(window) && !Harness::WantQuit())
    {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        Harness::Poll(); // injects synthetic events AFTER backend, before NewFrame
        ImGui::NewFrame();

        build_ui();

        ImGui::Render();
        glViewport(0, 0, (int)(io.DisplaySize.x * io.DisplayFramebufferScale.x),
                   (int)(io.DisplaySize.y * io.DisplayFramebufferScale.y));
        glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
        Harness::EndFrame();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    Harness::Shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
