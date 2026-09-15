#pragma once
#include "CircuitData.h"

namespace LogicSim {

class CircuitEditor {
public:
    CircuitEditor();
    ~CircuitEditor() = default;

    // Call this inside an ImGui window or full-screen view
    void Render(CircuitGraph& graph, float deltaTime);

    CanvasState& GetCanvasState() { return m_canvas; }
    WiringState& GetWiringState() { return m_wiring; }

private:
    CanvasState m_canvas;
    WiringState m_wiring;
    float m_animTimer = 0.0f;

    // Internal rendering helpers
    void HandleCanvasInput(CircuitGraph& graph);
    void DrawBackgroundGrid(ImDrawList* drawList);
    void DrawLinks(CircuitGraph& graph, ImDrawList* drawList);
    void DrawNodes(CircuitGraph& graph, ImDrawList* drawList);
    void DrawWiringInteraction(CircuitGraph& graph, ImDrawList* drawList);
    void DrawMarqueeSelection(CircuitGraph& graph, ImDrawList* drawList);
    void DrawContextMenu(CircuitGraph& graph);

    // Vector Icon Drawing Helpers (pure ImDrawList vector math)
    void DrawVectorGateAND(ImDrawList* drawList, ImVec2 min, ImVec2 max, ImU32 color);
    void DrawVectorGateOR(ImDrawList* drawList, ImVec2 min, ImVec2 max, ImU32 color);
    void DrawVectorGateNOT(ImDrawList* drawList, ImVec2 min, ImVec2 max, ImU32 color);
    void DrawVectorGateXOR(ImDrawList* drawList, ImVec2 min, ImVec2 max, ImU32 color);
    void DrawVectorGateNAND(ImDrawList* drawList, ImVec2 min, ImVec2 max, ImU32 color);
    void DrawVector7Segment(ImDrawList* drawList, ImVec2 min, ImVec2 max, uint8_t mask);
    void DrawVectorLED(ImDrawList* drawList, ImVec2 center, float radius, bool active);
};

// Standalone function matching requirement 3:
// "The main RenderCircuitEditor() function including canvas interaction, splitter rendering, and the wiring state machine."
void RenderCircuitEditor(CircuitGraph& graph, CanvasState& canvas, WiringState& wiring, float deltaTime);

} // namespace LogicSim
