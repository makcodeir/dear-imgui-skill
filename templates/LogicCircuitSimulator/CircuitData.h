#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include "imgui.h"

namespace LogicSim {

enum class PinKind {
    Input,
    Output
};

enum class LogicValue : uint8_t {
    Low = 0,
    High = 1,
    Floating = 2
};

struct Pin {
    uint32_t id = 0;
    uint32_t nodeId = 0;
    std::string name;
    PinKind kind = PinKind::Input;
    LogicValue state = LogicValue::Low;
    
    // Relative offset to node top-left in World Space
    ImVec2 localOffset = ImVec2(0.0f, 0.0f);
};

enum class NodeType {
    InputToggle,
    Clock,
    GateAND,
    GateOR,
    GateNOT,
    GateXOR,
    GateNAND,
    OutputLED,
    Display7Segment
};

struct Node {
    uint32_t id = 0;
    std::string title;
    NodeType type = NodeType::GateAND;
    ImVec2 worldPos = ImVec2(0.0f, 0.0f);
    ImVec2 size = ImVec2(100.0f, 60.0f);
    bool selected = false;

    std::vector<Pin> inputs;
    std::vector<Pin> outputs;

    // Component-specific parameters
    bool toggleState = false;          // For InputToggle
    float clockFrequencyHz = 1.0f;     // For Clock generator
    float clockTimer = 0.0f;           // Internal clock accumulator
    
    // Internal display cache
    uint8_t segmentMask = 0;           // For Display7Segment (a..g)
};

struct Link {
    uint32_t id = 0;
    uint32_t fromPinId = 0; // Must be Output Pin
    uint32_t toPinId = 0;   // Must be Input Pin
    LogicValue state = LogicValue::Low;
};

// Canvas Transformation and Viewport State
struct CanvasState {
    ImVec2 pan = ImVec2(0.0f, 0.0f);
    float zoom = 1.0f;
    const float minZoom = 0.2f;
    const float maxZoom = 3.0f;

    ImVec2 screenOrigin = ImVec2(0.0f, 0.0f);
    ImVec2 canvasSize = ImVec2(0.0f, 0.0f);

    inline ImVec2 WorldToScreen(const ImVec2& worldPos) const {
        return ImVec2(
            screenOrigin.x + pan.x + (worldPos.x * zoom),
            screenOrigin.y + pan.y + (worldPos.y * zoom)
        );
    }

    inline ImVec2 ScreenToWorld(const ImVec2& screenPos) const {
        return ImVec2(
            (screenPos.x - screenOrigin.x - pan.x) / zoom,
            (screenPos.y - screenOrigin.y - pan.y) / zoom
        );
    }
};

enum class InteractionState {
    Idle,
    DraggingCanvas,
    DraggingNode,
    DraggingWire,
    BoxSelecting
};

struct WiringState {
    InteractionState state = InteractionState::Idle;
    uint32_t activePinId = 0;      // Starting pin when dragging wire
    ImVec2 wireDragScreenPos = ImVec2(0.0f, 0.0f);
    uint32_t hoverPinId = 0;       // Valid target pin currently hovered
    ImVec2 boxSelectStartWorld = ImVec2(0.0f, 0.0f);
    ImVec2 boxSelectEndWorld = ImVec2(0.0f, 0.0f);
};

struct CircuitGraph {
    uint32_t nextId = 1;
    std::vector<Node> nodes;
    std::vector<Link> links;
    std::vector<uint32_t> drawOrder; // Node IDs in render order (back to front)

    uint32_t AllocateId() { return nextId++; }

    Node* FindNode(uint32_t nodeId);
    const Node* FindNode(uint32_t nodeId) const;

    Pin* FindPin(uint32_t pinId);
    const Pin* FindPin(uint32_t pinId) const;

    Link* FindLink(uint32_t linkId);
    bool HasIncomingLink(uint32_t inputPinId) const;
    void RemoveLink(uint32_t linkId);
    void RemoveLinksConnectedToPin(uint32_t pinId);
    void RemoveNode(uint32_t nodeId);

    void BringNodeToFront(uint32_t nodeId);

    // Factory methods to create standardized nodes
    Node& AddToggleNode(ImVec2 pos, const char* name = "SWITCH");
    Node& AddClockNode(ImVec2 pos, float freqHz = 2.0f);
    Node& AddGateNode(NodeType type, ImVec2 pos);
    Node& AddOutputLEDNode(ImVec2 pos, const char* name = "PROBE");
    Node& Add7SegmentNode(ImVec2 pos);
};

} // namespace LogicSim
