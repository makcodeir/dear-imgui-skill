#include "CircuitEngine.h"
#include <algorithm>
#include <cmath>

namespace LogicSim {

Node* CircuitGraph::FindNode(uint32_t nodeId) {
    for (auto& node : nodes) {
        if (node.id == nodeId) return &node;
    }
    return nullptr;
}

const Node* CircuitGraph::FindNode(uint32_t nodeId) const {
    for (const auto& node : nodes) {
        if (node.id == nodeId) return &node;
    }
    return nullptr;
}

Pin* CircuitGraph::FindPin(uint32_t pinId) {
    for (auto& node : nodes) {
        for (auto& pin : node.inputs) {
            if (pin.id == pinId) return &pin;
        }
        for (auto& pin : node.outputs) {
            if (pin.id == pinId) return &pin;
        }
    }
    return nullptr;
}

const Pin* CircuitGraph::FindPin(uint32_t pinId) const {
    for (const auto& node : nodes) {
        for (const auto& pin : node.inputs) {
            if (pin.id == pinId) return &pin;
        }
        for (const auto& pin : node.outputs) {
            if (pin.id == pinId) return &pin;
        }
    }
    return nullptr;
}

Link* CircuitGraph::FindLink(uint32_t linkId) {
    for (auto& link : links) {
        if (link.id == linkId) return &link;
    }
    return nullptr;
}

bool CircuitGraph::HasIncomingLink(uint32_t inputPinId) const {
    for (const auto& link : links) {
        if (link.toPinId == inputPinId) return true;
    }
    return false;
}

void CircuitGraph::RemoveLink(uint32_t linkId) {
    links.erase(
        std::remove_if(links.begin(), links.end(), [linkId](const Link& l) { return l.id == linkId; }),
        links.end()
    );
}

void CircuitGraph::RemoveLinksConnectedToPin(uint32_t pinId) {
    links.erase(
        std::remove_if(links.begin(), links.end(), [pinId](const Link& l) {
            return l.fromPinId == pinId || l.toPinId == pinId;
        }),
        links.end()
    );
}

void CircuitGraph::RemoveNode(uint32_t nodeId) {
    Node* node = FindNode(nodeId);
    if (!node) return;

    for (const auto& pin : node->inputs) {
        RemoveLinksConnectedToPin(pin.id);
    }
    for (const auto& pin : node->outputs) {
        RemoveLinksConnectedToPin(pin.id);
    }

    nodes.erase(
        std::remove_if(nodes.begin(), nodes.end(), [nodeId](const Node& n) { return n.id == nodeId; }),
        nodes.end()
    );

    drawOrder.erase(
        std::remove_if(drawOrder.begin(), drawOrder.end(), [nodeId](uint32_t id) { return id == nodeId; }),
        drawOrder.end()
    );
}

void CircuitGraph::BringNodeToFront(uint32_t nodeId) {
    auto it = std::find(drawOrder.begin(), drawOrder.end(), nodeId);
    if (it != drawOrder.end()) {
        drawOrder.erase(it);
        drawOrder.push_back(nodeId);
    }
}

// ---------------- Standard Node Construction ----------------

Node& CircuitGraph::AddToggleNode(ImVec2 pos, const char* name) {
    Node node;
    node.id = AllocateId();
    node.title = name;
    node.type = NodeType::InputToggle;
    node.worldPos = pos;
    node.size = ImVec2(100.0f, 60.0f);
    node.toggleState = false;

    Pin outPin;
    outPin.id = AllocateId();
    outPin.nodeId = node.id;
    outPin.name = "Q";
    outPin.kind = PinKind::Output;
    outPin.state = LogicValue::Low;
    outPin.localOffset = ImVec2(node.size.x, node.size.y * 0.5f);
    node.outputs.push_back(outPin);

    nodes.push_back(node);
    drawOrder.push_back(node.id);
    return nodes.back();
}

Node& CircuitGraph::AddClockNode(ImVec2 pos, float freqHz) {
    Node node;
    node.id = AllocateId();
    node.title = "CLOCK";
    node.type = NodeType::Clock;
    node.worldPos = pos;
    node.size = ImVec2(120.0f, 75.0f);
    node.clockFrequencyHz = freqHz;
    node.clockTimer = 0.0f;

    Pin outPin;
    outPin.id = AllocateId();
    outPin.nodeId = node.id;
    outPin.name = "CLK";
    outPin.kind = PinKind::Output;
    outPin.state = LogicValue::Low;
    outPin.localOffset = ImVec2(node.size.x, node.size.y * 0.5f);
    node.outputs.push_back(outPin);

    nodes.push_back(node);
    drawOrder.push_back(node.id);
    return nodes.back();
}

Node& CircuitGraph::AddGateNode(NodeType type, ImVec2 pos) {
    Node node;
    node.id = AllocateId();
    node.type = type;
    node.worldPos = pos;
    
    if (type == NodeType::GateNOT) {
        node.title = "NOT";
        node.size = ImVec2(90.0f, 55.0f);

        Pin inPin;
        inPin.id = AllocateId();
        inPin.nodeId = node.id;
        inPin.name = "A";
        inPin.kind = PinKind::Input;
        inPin.localOffset = ImVec2(0.0f, node.size.y * 0.5f);
        node.inputs.push_back(inPin);
    } else {
        node.size = ImVec2(100.0f, 65.0f);
        switch (type) {
            case NodeType::GateAND:  node.title = "AND"; break;
            case NodeType::GateOR:   node.title = "OR"; break;
            case NodeType::GateXOR:  node.title = "XOR"; break;
            case NodeType::GateNAND: node.title = "NAND"; break;
            default: node.title = "GATE"; break;
        }

        Pin inA;
        inA.id = AllocateId();
        inA.nodeId = node.id;
        inA.name = "A";
        inA.kind = PinKind::Input;
        inA.localOffset = ImVec2(0.0f, node.size.y * 0.32f);
        node.inputs.push_back(inA);

        Pin inB;
        inB.id = AllocateId();
        inB.nodeId = node.id;
        inB.name = "B";
        inB.kind = PinKind::Input;
        inB.localOffset = ImVec2(0.0f, node.size.y * 0.68f);
        node.inputs.push_back(inB);
    }

    Pin outQ;
    outQ.id = AllocateId();
    outQ.nodeId = node.id;
    outQ.name = "Q";
    outQ.kind = PinKind::Output;
    outQ.localOffset = ImVec2(node.size.x, node.size.y * 0.5f);
    node.outputs.push_back(outQ);

    nodes.push_back(node);
    drawOrder.push_back(node.id);
    return nodes.back();
}

Node& CircuitGraph::AddOutputLEDNode(ImVec2 pos, const char* name) {
    Node node;
    node.id = AllocateId();
    node.title = name;
    node.type = NodeType::OutputLED;
    node.worldPos = pos;
    node.size = ImVec2(90.0f, 60.0f);

    Pin inPin;
    inPin.id = AllocateId();
    inPin.nodeId = node.id;
    inPin.name = "IN";
    inPin.kind = PinKind::Input;
    inPin.localOffset = ImVec2(0.0f, node.size.y * 0.5f);
    node.inputs.push_back(inPin);

    nodes.push_back(node);
    drawOrder.push_back(node.id);
    return nodes.back();
}

Node& CircuitGraph::Add7SegmentNode(ImVec2 pos) {
    Node node;
    node.id = AllocateId();
    node.title = "7-SEGMENT";
    node.type = NodeType::Display7Segment;
    node.worldPos = pos;
    node.size = ImVec2(120.0f, 130.0f);
    node.segmentMask = 0;

    const char* pinNames[4] = { "D0 (1)", "D1 (2)", "D2 (4)", "D3 (8)" };
    for (int i = 0; i < 4; ++i) {
        Pin pin;
        pin.id = AllocateId();
        pin.nodeId = node.id;
        pin.name = pinNames[i];
        pin.kind = PinKind::Input;
        pin.localOffset = ImVec2(0.0f, 28.0f + (float)i * 24.0f);
        node.inputs.push_back(pin);
    }

    nodes.push_back(node);
    drawOrder.push_back(node.id);
    return nodes.back();
}

// ---------------- Logic Simulation Engine Implementation ----------------

uint8_t LogicEngine::BCDTo7Segment(uint8_t val) {
    // 7-segment bitmask (bit 0: a, bit 1: b, ..., bit 6: g)
    // 0: 0x3F (a,b,c,d,e,f)
    // 1: 0x06 (b,c)
    // 2: 0x5B (a,b,d,e,g)
    // 3: 0x4F (a,b,c,d,g)
    // 4: 0x66 (b,c,f,g)
    // 5: 0x6D (a,c,d,f,g)
    // 6: 0x7D (a,c,d,e,f,g)
    // 7: 0x07 (a,b,c)
    // 8: 0x7F (a,b,c,d,e,f,g)
    // 9: 0x6F (a,b,c,d,f,g)
    // A: 0x77
    // b: 0x7C
    // C: 0x39
    // d: 0x5E
    // E: 0x79
    // F: 0x71
    static const uint8_t bcdLut[16] = {
        0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07,
        0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71
    };
    return (val < 16) ? bcdLut[val] : 0x00;
}

void LogicEngine::Step(CircuitGraph& graph, float deltaTime, int maxPropagationIterations) {
    // Step 1: Advance clocks and determine primary input states
    for (auto& node : graph.nodes) {
        if (node.type == NodeType::InputToggle) {
            if (!node.outputs.empty()) {
                node.outputs[0].state = node.toggleState ? LogicValue::High : LogicValue::Low;
            }
        } else if (node.type == NodeType::Clock) {
            float period = (node.clockFrequencyHz > 0.05f) ? (1.0f / node.clockFrequencyHz) : 1.0f;
            node.clockTimer += deltaTime;
            while (node.clockTimer >= period) {
                node.clockTimer -= period;
            }
            bool high = (node.clockTimer < (period * 0.5f));
            if (!node.outputs.empty()) {
                node.outputs[0].state = high ? LogicValue::High : LogicValue::Low;
            }
        }
    }

    // Step 2: Iterative propagation across the network
    // Propagates output states through links to input pins, then updates logic nodes
    for (int iter = 0; iter < maxPropagationIterations; ++iter) {
        bool stateChanged = false;

        // Propagate signals across links
        for (auto& link : graph.links) {
            const Pin* srcPin = graph.FindPin(link.fromPinId);
            Pin* dstPin = graph.FindPin(link.toPinId);
            if (srcPin && dstPin) {
                link.state = srcPin->state;
                if (dstPin->state != srcPin->state) {
                    dstPin->state = srcPin->state;
                    stateChanged = true;
                }
            }
        }

        // Float disconnected input pins to Low
        for (auto& node : graph.nodes) {
            for (auto& inPin : node.inputs) {
                if (!graph.HasIncomingLink(inPin.id)) {
                    if (inPin.state != LogicValue::Low) {
                        inPin.state = LogicValue::Low;
                        stateChanged = true;
                    }
                }
            }
        }

        // Evaluate logic components
        for (auto& node : graph.nodes) {
            switch (node.type) {
                case NodeType::GateAND: {
                    if (node.inputs.size() >= 2 && !node.outputs.empty()) {
                        bool a = (node.inputs[0].state == LogicValue::High);
                        bool b = (node.inputs[1].state == LogicValue::High);
                        LogicValue nextVal = (a && b) ? LogicValue::High : LogicValue::Low;
                        if (node.outputs[0].state != nextVal) {
                            node.outputs[0].state = nextVal;
                            stateChanged = true;
                        }
                    }
                    break;
                }
                case NodeType::GateOR: {
                    if (node.inputs.size() >= 2 && !node.outputs.empty()) {
                        bool a = (node.inputs[0].state == LogicValue::High);
                        bool b = (node.inputs[1].state == LogicValue::High);
                        LogicValue nextVal = (a || b) ? LogicValue::High : LogicValue::Low;
                        if (node.outputs[0].state != nextVal) {
                            node.outputs[0].state = nextVal;
                            stateChanged = true;
                        }
                    }
                    break;
                }
                case NodeType::GateNOT: {
                    if (!node.inputs.empty() && !node.outputs.empty()) {
                        bool a = (node.inputs[0].state == LogicValue::High);
                        LogicValue nextVal = (!a) ? LogicValue::High : LogicValue::Low;
                        if (node.outputs[0].state != nextVal) {
                            node.outputs[0].state = nextVal;
                            stateChanged = true;
                        }
                    }
                    break;
                }
                case NodeType::GateXOR: {
                    if (node.inputs.size() >= 2 && !node.outputs.empty()) {
                        bool a = (node.inputs[0].state == LogicValue::High);
                        bool b = (node.inputs[1].state == LogicValue::High);
                        LogicValue nextVal = (a ^ b) ? LogicValue::High : LogicValue::Low;
                        if (node.outputs[0].state != nextVal) {
                            node.outputs[0].state = nextVal;
                            stateChanged = true;
                        }
                    }
                    break;
                }
                case NodeType::GateNAND: {
                    if (node.inputs.size() >= 2 && !node.outputs.empty()) {
                        bool a = (node.inputs[0].state == LogicValue::High);
                        bool b = (node.inputs[1].state == LogicValue::High);
                        LogicValue nextVal = !(a && b) ? LogicValue::High : LogicValue::Low;
                        if (node.outputs[0].state != nextVal) {
                            node.outputs[0].state = nextVal;
                            stateChanged = true;
                        }
                    }
                    break;
                }
                case NodeType::OutputLED: {
                    // Logic probe simply reflects input pin state
                    break;
                }
                case NodeType::Display7Segment: {
                    uint8_t bcd = 0;
                    for (size_t i = 0; i < node.inputs.size() && i < 4; ++i) {
                        if (node.inputs[i].state == LogicValue::High) {
                            bcd |= (1 << i);
                        }
                    }
                    node.segmentMask = BCDTo7Segment(bcd);
                    break;
                }
                default:
                    break;
            }
        }

        // If circuit reached stable convergence, exit early
        if (!stateChanged) {
            break;
        }
    }
}

} // namespace LogicSim
