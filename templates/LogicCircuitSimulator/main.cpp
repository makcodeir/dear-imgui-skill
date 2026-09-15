#include "Logic/CircuitData.h"
#include "Logic/CircuitEngine.h"
#include "Logic/CircuitEditor.h"
#include "imgui.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef HEADLESS_SELFTEST

int main(int, char**) {
    printf("[LogicSim Selftest] Initializing headless ImGui context...\n");
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.DisplaySize = ImVec2(1920.0f, 1080.0f);
    io.DeltaTime = 1.0f / 60.0f;

    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    LogicSim::CircuitGraph graph;
    LogicSim::CircuitEditor editor;

    // 1. Build an XOR subcircuit from basic gates to test logic evaluation
    // A, B -> XOR gate -> Probe LED
    uint32_t idA = graph.AddToggleNode(ImVec2(100, 100), "INPUT A").id;
    uint32_t idB = graph.AddToggleNode(ImVec2(100, 250), "INPUT B").id;
    uint32_t idXor = graph.AddGateNode(LogicSim::NodeType::GateXOR, ImVec2(350, 180)).id;
    uint32_t idProbe = graph.AddOutputLEDNode(ImVec2(600, 180), "XOR OUT").id;

    // Add Clock and 7-Segment Display
    uint32_t idClk = graph.AddClockNode(ImVec2(100, 400), 2.0f).id;
    uint32_t idSeg7 = graph.Add7SegmentNode(ImVec2(600, 350)).id;

    // Query stable pointers after all additions are complete
    auto* inA = graph.FindNode(idA);
    auto* inB = graph.FindNode(idB);
    auto* xorGate = graph.FindNode(idXor);
    auto* probe = graph.FindNode(idProbe);
    auto* clk = graph.FindNode(idClk);
    auto* seg7 = graph.FindNode(idSeg7);

    // Connect inA -> xorGate.in0
    graph.links.push_back({ graph.AllocateId(), inA->outputs[0].id, xorGate->inputs[0].id, LogicSim::LogicValue::Low });
    // Connect inB -> xorGate.in1
    graph.links.push_back({ graph.AllocateId(), inB->outputs[0].id, xorGate->inputs[1].id, LogicSim::LogicValue::Low });
    // Connect xorGate.out -> probe.in
    graph.links.push_back({ graph.AllocateId(), xorGate->outputs[0].id, probe->inputs[0].id, LogicSim::LogicValue::Low });
    // Connect clk -> seg7 pin 0
    graph.links.push_back({ graph.AllocateId(), clk->outputs[0].id, seg7->inputs[0].id, LogicSim::LogicValue::Low });

    printf("[LogicSim Selftest] Graph created with %zu nodes, %zu links.\n", graph.nodes.size(), graph.links.size());

    // Test Case 1: 0 XOR 0 == 0
    inA->toggleState = false;
    inB->toggleState = false;
    LogicSim::LogicEngine::Step(graph, 1.0f / 60.0f);
    if (probe->inputs[0].state != LogicSim::LogicValue::Low) {
        fprintf(stderr, "FAIL: 0 XOR 0 produced HIGH\n");
        return 1;
    }
    printf("  [PASS] 0 XOR 0 == LOW\n");

    // Test Case 2: 1 XOR 0 == 1
    inA->toggleState = true;
    inB->toggleState = false;
    LogicSim::LogicEngine::Step(graph, 1.0f / 60.0f);
    if (probe->inputs[0].state != LogicSim::LogicValue::High) {
        fprintf(stderr, "FAIL: 1 XOR 0 produced LOW\n");
        return 1;
    }
    printf("  [PASS] 1 XOR 0 == HIGH\n");

    // Test Case 3: 1 XOR 1 == 0
    inA->toggleState = true;
    inB->toggleState = true;
    LogicSim::LogicEngine::Step(graph, 1.0f / 60.0f);
    if (probe->inputs[0].state != LogicSim::LogicValue::Low) {
        fprintf(stderr, "FAIL: 1 XOR 1 produced HIGH\n");
        return 1;
    }
    printf("  [PASS] 1 XOR 1 == LOW\n");

    // Test Frame Rendering (checks ImDrawListSplitter, canvas math, vector gate drawing, zero assertions)
    for (int frame = 0; frame < 10; ++frame) {
        ImGui::NewFrame();
        LogicSim::LogicEngine::Step(graph, io.DeltaTime);
        editor.Render(graph, io.DeltaTime);
        ImGui::Render();
    }
    printf("  [PASS] 10 simulation + render frames executed cleanly with no crashes/asserts.\n");

    ImGui::DestroyContext();
    printf("[LogicSim Selftest] ALL TESTS PASSED.\n");
    return 0;
}

#else

#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

static void GlfwErrorCallback(int error, const char* description) {
    fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

int main(int, char**) {
    glfwSetErrorCallback(GlfwErrorCallback);
    if (!glfwInit())
        return 1;

    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    GLFWwindow* window = glfwCreateWindow(1600, 950, "Real-Time Digital Logic Circuit Simulator - Pure Dear ImGui", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable VSync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    // Style adjustments for clean high-tech dark aesthetics
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 6.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Instantiate Circuit Graph & Editor
    LogicSim::CircuitGraph graph;
    LogicSim::CircuitEditor editor;

    // Create a demo circuit: Half-Adder + Clock + 7-Segment display
    // Add all nodes first, capturing their unique IDs
    uint32_t idSwA = graph.AddToggleNode(ImVec2(80, 150), "A (TOGGLE)").id;
    uint32_t idSwB = graph.AddToggleNode(ImVec2(80, 320), "B (TOGGLE)").id;
    uint32_t idXor = graph.AddGateNode(LogicSim::NodeType::GateXOR, ImVec2(340, 180)).id;
    uint32_t idAnd = graph.AddGateNode(LogicSim::NodeType::GateAND, ImVec2(340, 340)).id;
    uint32_t idSum = graph.AddOutputLEDNode(ImVec2(620, 180), "SUM (S)").id;
    uint32_t idCarry = graph.AddOutputLEDNode(ImVec2(620, 340), "CARRY (C)").id;
    uint32_t idClk = graph.AddClockNode(ImVec2(80, 520), 2.0f).id;
    uint32_t idNot = graph.AddGateNode(LogicSim::NodeType::GateNOT, ImVec2(340, 520)).id;
    uint32_t idSeg7 = graph.Add7SegmentNode(ImVec2(620, 520)).id;

    // Lookup stable pointers after vector reallocations are complete
    auto* swA = graph.FindNode(idSwA);
    auto* swB = graph.FindNode(idSwB);
    auto* xorGate = graph.FindNode(idXor);
    auto* andGate = graph.FindNode(idAnd);
    auto* sumLed = graph.FindNode(idSum);
    auto* carryLed = graph.FindNode(idCarry);
    auto* clk = graph.FindNode(idClk);
    auto* notGate = graph.FindNode(idNot);
    auto* seg7 = graph.FindNode(idSeg7);

    swA->toggleState = true;

    // Wire Half-Adder
    graph.links.push_back({ graph.AllocateId(), swA->outputs[0].id, xorGate->inputs[0].id, LogicSim::LogicValue::Low });
    graph.links.push_back({ graph.AllocateId(), swB->outputs[0].id, xorGate->inputs[1].id, LogicSim::LogicValue::Low });
    graph.links.push_back({ graph.AllocateId(), swA->outputs[0].id, andGate->inputs[0].id, LogicSim::LogicValue::Low });
    graph.links.push_back({ graph.AllocateId(), swB->outputs[0].id, andGate->inputs[1].id, LogicSim::LogicValue::Low });
    graph.links.push_back({ graph.AllocateId(), xorGate->outputs[0].id, sumLed->inputs[0].id, LogicSim::LogicValue::Low });
    graph.links.push_back({ graph.AllocateId(), andGate->outputs[0].id, carryLed->inputs[0].id, LogicSim::LogicValue::Low });

    // Wire Clock + NOT + 7-Segment Display Demo
    graph.links.push_back({ graph.AllocateId(), clk->outputs[0].id, notGate->inputs[0].id, LogicSim::LogicValue::Low });
    graph.links.push_back({ graph.AllocateId(), clk->outputs[0].id, seg7->inputs[0].id, LogicSim::LogicValue::Low });
    graph.links.push_back({ graph.AllocateId(), notGate->outputs[0].id, seg7->inputs[1].id, LogicSim::LogicValue::Low });

    // Main render loop
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // 1. Simulation Step (decoupled from rendering, dynamic delta time)
        LogicSim::LogicEngine::Step(graph, io.DeltaTime);

        // 2. Fullscreen Editor Window
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus;

        if (ImGui::Begin("Digital Logic Simulator Canvas", nullptr, windowFlags)) {
            editor.Render(graph, io.DeltaTime);
        }
        ImGui::End();

        // Rendering backend
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

#endif
