#pragma once
#include <stdint.h>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include "imgui.h"

// ============================================================================
// Logic Circuit Simulator Template — Pure Dear ImGui (Zero External Node Libs)
// Features:
//   - Pan & Zoom Canvas Math (Screen <-> World) with cursor-centered zoom
//   - 4-Channel ImDrawListSplitter (Grid/Marquee -> Wires -> Bodies -> Pins/Widgets)
//   - Custom vector logic gate shapes (AND, OR, NOT, XOR, NAND)
//   - Interactive nodes (Toggle switch, Clock, LED probe, 7-Segment display)
//   - Decoupled real-time iterative simulation engine
// ============================================================================

namespace CircuitSim {

enum class LogicValue : uint8_t {
    Low = 0,
    High = 1,
    Floating = 2
};

enum class PinKind { Input, Output };

struct Pin {
    uint32_t id = 0;
    uint32_t nodeId = 0;
    std::string name;
    PinKind kind = PinKind::Input;
    LogicValue state = LogicValue::Low;
    ImVec2 localOffset = ImVec2(0.0f, 0.0f); // World offset from node top-left
};

enum class NodeType {
    ToggleSwitch,
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

    // Component parameters
    bool toggleState = false;
    float clockFreqHz = 1.0f;
    float clockTimer = 0.0f;
    uint8_t segmentMask = 0; // 7-Segment decoded mask (bits 0..6 -> a..g)
};

struct Link {
    uint32_t id = 0;
    uint32_t fromPinId = 0; // Output Pin
    uint32_t toPinId = 0;   // Input Pin
    LogicValue state = LogicValue::Low;
};

struct CanvasState {
    ImVec2 pan = ImVec2(100.0f, 100.0f);
    float zoom = 1.0f;
    const float minZoom = 0.2f;
    const float maxZoom = 3.0f;
    ImVec2 screenOrigin = ImVec2(0.0f, 0.0f);
    ImVec2 canvasSize = ImVec2(0.0f, 0.0f);

    inline ImVec2 WorldToScreen(const ImVec2& worldPos) const {
        return ImVec2(screenOrigin.x + pan.x + (worldPos.x * zoom),
                      screenOrigin.y + pan.y + (worldPos.y * zoom));
    }
    inline ImVec2 ScreenToWorld(const ImVec2& screenPos) const {
        return ImVec2((screenPos.x - screenOrigin.x - pan.x) / zoom,
                      (screenPos.y - screenOrigin.y - pan.y) / zoom);
    }
};

enum class InteractionState {
    Idle,
    DraggingNode,
    DraggingWire,
    BoxSelecting
};

struct CircuitGraph {
    uint32_t nextId = 1;
    std::vector<Node> nodes;
    std::vector<Link> links;
    std::vector<uint32_t> drawOrder;

    uint32_t AllocateId() { return nextId++; }

    Node* FindNode(uint32_t nodeId) {
        for (auto& n : nodes) if (n.id == nodeId) return &n;
        return nullptr;
    }
    Pin* FindPin(uint32_t pinId) {
        for (auto& n : nodes) {
            for (auto& p : n.inputs) if (p.id == pinId) return &p;
            for (auto& p : n.outputs) if (p.id == pinId) return &p;
        }
        return nullptr;
    }
    bool HasIncomingLink(uint32_t inputPinId) const {
        for (const auto& l : links) if (l.toPinId == inputPinId) return true;
        return false;
    }
    void BringToFront(uint32_t nodeId) {
        auto it = std::find(drawOrder.begin(), drawOrder.end(), nodeId);
        if (it != drawOrder.end()) {
            drawOrder.erase(it);
            drawOrder.push_back(nodeId);
        }
    }
};

// Simulation engine: resolves multi-gate propagation in iterative relaxation steps
inline void StepSimulation(CircuitGraph& graph, float deltaTime, int maxIterations = 16) {
    // 1. Advance clocks
    for (auto& node : graph.nodes) {
        if (node.type == NodeType::Clock && !node.outputs.empty()) {
            node.clockTimer += deltaTime;
            float period = (node.clockFreqHz > 0.001f) ? (1.0f / node.clockFreqHz) : 1.0f;
            float phase = std::fmod(node.clockTimer, period);
            node.outputs[0].state = (phase < period * 0.5f) ? LogicValue::High : LogicValue::Low;
        } else if (node.type == NodeType::ToggleSwitch && !node.outputs.empty()) {
            node.outputs[0].state = node.toggleState ? LogicValue::High : LogicValue::Low;
        }
    }

    // 2. Iterative propagation
    for (int iter = 0; iter < maxIterations; ++iter) {
        bool changed = false;

        // Propagate signals along links
        for (auto& link : graph.links) {
            Pin* from = graph.FindPin(link.fromPinId);
            Pin* to = graph.FindPin(link.toPinId);
            if (from && to) {
                link.state = from->state;
                if (to->state != from->state) {
                    to->state = from->state;
                    changed = true;
                }
            }
        }

        // Evaluate gate outputs
        for (auto& node : graph.nodes) {
            switch (node.type) {
                case NodeType::GateAND: {
                    if (node.inputs.size() >= 2 && !node.outputs.empty()) {
                        LogicValue next = (node.inputs[0].state == LogicValue::High && node.inputs[1].state == LogicValue::High) ? LogicValue::High : LogicValue::Low;
                        if (node.outputs[0].state != next) { node.outputs[0].state = next; changed = true; }
                    }
                    break;
                }
                case NodeType::GateOR: {
                    if (node.inputs.size() >= 2 && !node.outputs.empty()) {
                        LogicValue next = (node.inputs[0].state == LogicValue::High || node.inputs[1].state == LogicValue::High) ? LogicValue::High : LogicValue::Low;
                        if (node.outputs[0].state != next) { node.outputs[0].state = next; changed = true; }
                    }
                    break;
                }
                case NodeType::GateNOT: {
                    if (!node.inputs.empty() && !node.outputs.empty()) {
                        LogicValue next = (node.inputs[0].state == LogicValue::High) ? LogicValue::Low : LogicValue::High;
                        if (node.outputs[0].state != next) { node.outputs[0].state = next; changed = true; }
                    }
                    break;
                }
                case NodeType::GateXOR: {
                    if (node.inputs.size() >= 2 && !node.outputs.empty()) {
                        bool h0 = (node.inputs[0].state == LogicValue::High);
                        bool h1 = (node.inputs[1].state == LogicValue::High);
                        LogicValue next = (h0 ^ h1) ? LogicValue::High : LogicValue::Low;
                        if (node.outputs[0].state != next) { node.outputs[0].state = next; changed = true; }
                    }
                    break;
                }
                case NodeType::GateNAND: {
                    if (node.inputs.size() >= 2 && !node.outputs.empty()) {
                        LogicValue next = !(node.inputs[0].state == LogicValue::High && node.inputs[1].state == LogicValue::High) ? LogicValue::High : LogicValue::Low;
                        if (node.outputs[0].state != next) { node.outputs[0].state = next; changed = true; }
                    }
                    break;
                }
                case NodeType::Display7Segment: {
                    // Decode 4-bit BCD to active segments
                    uint8_t val = 0;
                    for (size_t i = 0; i < node.inputs.size() && i < 4; ++i) {
                        if (node.inputs[i].state == LogicValue::High) val |= (1 << i);
                    }
                    static const uint8_t bcd[16] = {
                        0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07,
                        0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71
                    };
                    node.segmentMask = bcd[val & 0x0F];
                    break;
                }
                default: break;
            }
        }
        if (!changed) break;
    }
}

} // namespace CircuitSim
