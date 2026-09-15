// Dear ImGui — canonical GLFW + OpenGL3 application skeleton.
// This is the structure every example in examples/ uses. Copy it, then replace
// the "YOUR UI HERE" block with your own windows.
//
// Build (Linux, from the Dear ImGui repository root):
//   g++ -std=c++11 -I. -Ibackends app_layout_glfw.cpp
//       imgui.cpp imgui_demo.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp
//       backends/imgui_impl_glfw.cpp backends/imgui_impl_opengl3.cpp
//       -lglfw -lGL -ldl -lpthread -o myapp
//
// The five lifecycle beats, in order, are the contract with the library:
//   init:    CreateContext() -> ImGui_Impl*_Init()
//   frame:   ImGui_Impl*_NewFrame() -> ImGui::NewFrame() ... ImGui::Render()
//   draw:    ImGui_Impl*_RenderDrawData(ImGui::GetDrawData())
//   shutdown: ImGui_Impl*_Shutdown() -> DestroyContext()
// Getting these out of order is the #1 cause of asserts and blank windows.

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

int main(int, char**)
{
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    // GL 3.0 + GLSL 130 is a safe cross-platform default.
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
    GLFWwindow* window = glfwCreateWindow((int)(1280 * main_scale), (int)(800 * main_scale),
                                          "Dear ImGui app", nullptr, nullptr);
    if (window == nullptr)
        return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync

    // --- init ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    bool show_demo = true;
    ImVec4 clear_color = ImVec4(0.15f, 0.15f, 0.18f, 1.00f);

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0)
        {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }

        // --- frame ---
        // Renderer backend first, then platform backend, then NewFrame().
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // ================= YOUR UI HERE =================
        if (show_demo)
            ImGui::ShowDemoWindow(&show_demo);

        ImGui::SetNextWindowSize(ImVec2(400.0f, 220.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Main", nullptr, ImGuiWindowFlags_MenuBar))
        {
            if (ImGui::BeginMenuBar())
            {
                if (ImGui::BeginMenu("File"))
                {
                    if (ImGui::MenuItem("Quit", "Ctrl+Q"))
                        glfwSetWindowShouldClose(window, GLFW_TRUE);
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }

            static float value = 0.5f;
            ImGui::SliderFloat("Value", &value, 0.0f, 1.0f);
            ImGui::Text("FPS %.1f (%.3f ms/frame)", io.Framerate, 1000.0f / io.Framerate);
        }
        ImGui::End();
        // ================================================

        // --- draw ---
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w,
                     clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // --- shutdown (reverse order of init) ---
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
