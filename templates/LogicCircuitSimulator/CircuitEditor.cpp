#include "CircuitEditor.h"
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace LogicSim {

// Splitter channel definitions matching requirement 2:
// Channel 0: Background grid and marquee selection box
// Channel 1: Active connections and transient wiring cables (Cubic Bézier curves)
// Channel 2: Node bodies, borders, and custom logic gate vector iconography
// Channel 3: Pin terminals, hit-test halos, and inner interactive widgets (toggles, sliders, labels)
enum DrawChannel {
    Channel_Background = 0,
    Channel_Wires      = 1,
    Channel_NodeBodies = 2,
    Channel_PinsWidgets= 3,
    Channel_Count      = 4
};

static inline ImVec2 ImVec2Add(const ImVec2& a, const ImVec2& b) { return ImVec2(a.x + b.x, a.y + b.y); }
static inline ImVec2 ImVec2Sub(const ImVec2& a, const ImVec2& b) { return ImVec2(a.x - b.x, a.y - b.y); }
static inline ImVec2 ImVec2Mul(const ImVec2& a, float s) { return ImVec2(a.x * s, a.y * s); }
static inline float ImVec2DistSq(const ImVec2& a, const ImVec2& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

CircuitEditor::CircuitEditor() {
    m_canvas.pan = ImVec2(100.0f, 100.0f);
    m_canvas.zoom = 1.0f;
}

void CircuitEditor::Render(CircuitGraph& graph, float deltaTime) {
    m_animTimer += deltaTime;
    RenderCircuitEditor(graph, m_canvas, m_wiring, deltaTime);
}

void CircuitEditor::DrawBackgroundGrid(ImDrawList* drawList) {
    // Channel 0 is active for background
    const ImVec2 canvasMin = m_canvas.screenOrigin;
    const ImVec2 canvasMax = ImVec2Add(canvasMin, m_canvas.canvasSize);

    // Procedural background grid with major and minor lines
    const float minorBase = 16.0f;
    const float minorStep = minorBase * m_canvas.zoom;
    const float majorStep = minorStep * 5.0f;

    // Grid fading: smooth transition based on zoom level
    float gridAlpha = 1.0f;
    if (m_canvas.zoom < 0.5f) {
        gridAlpha = (m_canvas.zoom - m_canvas.minZoom) / (0.5f - m_canvas.minZoom);
        if (gridAlpha < 0.0f) gridAlpha = 0.0f;
    }

    ImU32 minorColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.18f, 0.19f, 0.22f, 0.45f * gridAlpha));
    ImU32 majorColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.24f, 0.26f, 0.30f, 0.85f * (0.4f + 0.6f * gridAlpha)));

    // Calculate grid alignment
    float startX = std::fmod(m_canvas.pan.x, majorStep);
    if (startX < 0.0f) startX += majorStep;
    float startY = std::fmod(m_canvas.pan.y, majorStep);
    if (startY < 0.0f) startY += majorStep;

    // Minor lines
    if (minorStep >= 8.0f) {
        float minStartX = std::fmod(m_canvas.pan.x, minorStep);
        if (minStartX < 0.0f) minStartX += minorStep;
        float minStartY = std::fmod(m_canvas.pan.y, minorStep);
        if (minStartY < 0.0f) minStartY += minorStep;

        for (float x = canvasMin.x + minStartX; x < canvasMax.x; x += minorStep) {
            drawList->AddLine(ImVec2(x, canvasMin.y), ImVec2(x, canvasMax.y), minorColor, 1.0f);
        }
        for (float y = canvasMin.y + minStartY; y < canvasMax.y; y += minorStep) {
            drawList->AddLine(ImVec2(canvasMin.x, y), ImVec2(canvasMax.x, y), minorColor, 1.0f);
        }
    }

    // Major lines
    for (float x = canvasMin.x + startX; x < canvasMax.x; x += majorStep) {
        drawList->AddLine(ImVec2(x, canvasMin.y), ImVec2(x, canvasMax.y), majorColor, 1.5f);
    }
    for (float y = canvasMin.y + startY; y < canvasMax.y; y += majorStep) {
        drawList->AddLine(ImVec2(canvasMin.x, y), ImVec2(canvasMax.x, y), majorColor, 1.5f);
    }
}

// Custom Vector Gate Renderers (Channel 2)
void CircuitEditor::DrawVectorGateAND(ImDrawList* drawList, ImVec2 min, ImVec2 max, ImU32 color) {
    float w = max.x - min.x;
    float h = max.y - min.y;
    float midX = min.x + w * 0.45f;
    float midY = min.y + h * 0.5f;
    float r = h * 0.5f;

    // Back flat line & top/bottom flats
    drawList->AddLine(ImVec2(min.x, min.y), ImVec2(min.x, max.y), color, 2.0f);
    drawList->AddLine(ImVec2(min.x, min.y), ImVec2(midX, min.y), color, 2.0f);
    drawList->AddLine(ImVec2(min.x, max.y), ImVec2(midX, max.y), color, 2.0f);

    // Curved front face (cubic bezier approximation of semicircle)
    ImVec2 p0(midX, min.y);
    ImVec2 p1(max.x, min.y);
    ImVec2 p2(max.x, max.y);
    ImVec2 p3(midX, max.y);
    drawList->AddBezierCubic(p0, p1, p2, p3, color, 2.0f, 16);
}

void CircuitEditor::DrawVectorGateOR(ImDrawList* drawList, ImVec2 min, ImVec2 max, ImU32 color) {
    float w = max.x - min.x;
    float h = max.y - min.y;

    // Curved back face
    ImVec2 b0(min.x, min.y);
    ImVec2 b1(min.x + w * 0.25f, min.y + h * 0.5f);
    ImVec2 b2(min.x, max.y);
    drawList->AddBezierQuadratic(b0, b1, b2, color, 2.0f, 12);

    // Curved top wing to pointed tip
    ImVec2 t0(min.x, min.y);
    ImVec2 t1(min.x + w * 0.55f, min.y);
    ImVec2 tTip(max.x, min.y + h * 0.5f);
    drawList->AddBezierQuadratic(t0, t1, tTip, color, 2.0f, 12);

    // Curved bottom wing to pointed tip
    ImVec2 d0(min.x, max.y);
    ImVec2 d1(min.x + w * 0.55f, max.y);
    drawList->AddBezierQuadratic(d0, d1, tTip, color, 2.0f, 12);
}

void CircuitEditor::DrawVectorGateNOT(ImDrawList* drawList, ImVec2 min, ImVec2 max, ImU32 color) {
    float w = max.x - min.x;
    float h = max.y - min.y;
    float bubbleR = 4.0f;

    // Triangle body
    ImVec2 p0(min.x, min.y);
    ImVec2 p1(min.x, max.y);
    ImVec2 p2(max.x - bubbleR * 2.0f, min.y + h * 0.5f);
    drawList->AddTriangle(p0, p1, p2, color, 2.0f);

    // Inversion circle
    ImVec2 bubbleCenter(max.x - bubbleR, min.y + h * 0.5f);
    drawList->AddCircle(bubbleCenter, bubbleR, color, 12, 1.8f);
}

void CircuitEditor::DrawVectorGateXOR(ImDrawList* drawList, ImVec2 min, ImVec2 max, ImU32 color) {
    float w = max.x - min.x;
    float h = max.y - min.y;

    // Additional curved back bar
    ImVec2 a0(min.x - w * 0.12f, min.y);
    ImVec2 a1(min.x + w * 0.13f, min.y + h * 0.5f);
    ImVec2 a2(min.x - w * 0.12f, max.y);
    drawList->AddBezierQuadratic(a0, a1, a2, color, 2.0f, 12);

    // OR body
    DrawVectorGateOR(drawList, min, max, color);
}

void CircuitEditor::DrawVectorGateNAND(ImDrawList* drawList, ImVec2 min, ImVec2 max, ImU32 color) {
    float h = max.y - min.y;
    float bubbleR = 4.0f;
    ImVec2 andMax(max.x - bubbleR * 2.0f, max.y);
    DrawVectorGateAND(drawList, min, andMax, color);

    ImVec2 bubbleCenter(max.x - bubbleR, min.y + h * 0.5f);
    drawList->AddCircle(bubbleCenter, bubbleR, color, 12, 1.8f);
}

void CircuitEditor::DrawVectorLED(ImDrawList* drawList, ImVec2 center, float radius, bool active) {
    ImU32 fillColor = active ? IM_COL32(50, 255, 80, 240) : IM_COL32(30, 45, 35, 200);
    ImU32 borderColor = active ? IM_COL32(180, 255, 190, 255) : IM_COL32(80, 95, 85, 255);

    // Dynamic glow effect when active
    if (active) {
        for (int i = 3; i >= 1; --i) {
            float glowR = radius + (float)i * 3.5f;
            ImU32 glowCol = IM_COL32(50, 255, 80, 45 / i);
            drawList->AddCircleFilled(center, glowR, glowCol, 24);
        }
    }

    drawList->AddCircleFilled(center, radius, fillColor, 20);
    drawList->AddCircle(center, radius, borderColor, 20, 2.0f);
}

void CircuitEditor::DrawVector7Segment(ImDrawList* drawList, ImVec2 min, ImVec2 max, uint8_t mask) {
    // 7 segments: a, b, c, d, e, f, g
    // Rendered crisply with ImDrawList polygons
    float w = max.x - min.x;
    float h = max.y - min.y;
    float t = w * 0.16f; // thickness

    ImU32 colOn  = IM_COL32(255, 45, 45, 240);
    ImU32 colOff = IM_COL32(45, 20, 20, 80);

    // Sub-rectangles for segments
    auto drawSeg = [&](int bit, ImVec2 p0, ImVec2 p1) {
        bool on = (mask & (1 << bit)) != 0;
        ImU32 col = on ? colOn : colOff;
        if (on) {
            // Glow
            drawList->AddRectFilled(ImVec2(p0.x - 2, p0.y - 2), ImVec2(p1.x + 2, p1.y + 2), IM_COL32(255, 40, 40, 40), 2.0f);
        }
        drawList->AddRectFilled(p0, p1, col, 2.0f);
    };

    // a: top horizontal
    drawSeg(0, ImVec2(min.x + t, min.y), ImVec2(max.x - t, min.y + t));
    // b: top right vertical
    drawSeg(1, ImVec2(max.x - t, min.y + t), ImVec2(max.x, min.y + h * 0.5f));
    // c: bottom right vertical
    drawSeg(2, ImVec2(max.x - t, min.y + h * 0.5f), ImVec2(max.x, max.y - t));
    // d: bottom horizontal
    drawSeg(3, ImVec2(min.x + t, max.y - t), ImVec2(max.x - t, max.y));
    // e: bottom left vertical
    drawSeg(4, ImVec2(min.x, min.y + h * 0.5f), ImVec2(min.x + t, max.y - t));
    // f: top left vertical
    drawSeg(5, ImVec2(min.x, min.y + t), ImVec2(min.x + t, min.y + h * 0.5f));
    // g: middle horizontal
    drawSeg(6, ImVec2(min.x + t, min.y + h * 0.5f - t * 0.5f), ImVec2(max.x - t, min.y + h * 0.5f + t * 0.5f));
}

// Standalone function implementation
void RenderCircuitEditor(CircuitGraph& graph, CanvasState& canvas, WiringState& wiring, float deltaTime) {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Canvas region
    canvas.screenOrigin = ImGui::GetCursorScreenPos();
    canvas.canvasSize = ImGui::GetContentRegionAvail();
    if (canvas.canvasSize.x < 50.0f) canvas.canvasSize.x = 50.0f;
    if (canvas.canvasSize.y < 50.0f) canvas.canvasSize.y = 50.0f;

    const ImVec2 canvasMin = canvas.screenOrigin;
    const ImVec2 canvasMax = ImVec2Add(canvasMin, canvas.canvasSize);

    // InvisibleButton covers entire canvas to capture mouse clicks/drags cleanly
    ImGui::InvisibleButton("##CircuitCanvas", canvas.canvasSize, 
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool isCanvasHovered = ImGui::IsItemHovered();
    const bool isCanvasActive = ImGui::IsItemActive();
    const ImVec2 mousePos = io.MousePos;

    // --- 1. Canvas Pan & Zoom Math ---
    if (isCanvasHovered) {
        // Cursor-centered zoom (mouse wheel) clamped between 0.2f and 3.0f
        if (io.MouseWheel != 0.0f) {
            float zoomDelta = io.MouseWheel * 0.1f * canvas.zoom;
            float newZoom = canvas.zoom + zoomDelta;
            if (newZoom < canvas.minZoom) newZoom = canvas.minZoom;
            if (newZoom > canvas.maxZoom) newZoom = canvas.maxZoom;

            if (newZoom != canvas.zoom) {
                // Invariant: WorldPos under mouse must stay at mousePos
                // ScreenPos = Origin + Pan + World * Zoom
                // World = (Mouse - Origin - Pan) / Zoom
                ImVec2 mouseWorldBefore = canvas.ScreenToWorld(mousePos);
                canvas.zoom = newZoom;
                // Solve for new Pan: Pan = Mouse - Origin - (mouseWorldBefore * newZoom)
                canvas.pan = ImVec2(
                    mousePos.x - canvas.screenOrigin.x - (mouseWorldBefore.x * canvas.zoom),
                    mousePos.y - canvas.screenOrigin.y - (mouseWorldBefore.y * canvas.zoom)
                );
            }
        }

        // Smooth pan via Middle Mouse Button drag or Right Mouse Button drag on empty canvas
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) || 
           (wiring.state == InteractionState::Idle && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f))) {
            canvas.pan = ImVec2Add(canvas.pan, io.MouseDelta);
        }
    }

    // --- Prepare ImDrawListSplitter ---
    ImDrawListSplitter splitter;
    splitter.Split(drawList, Channel_Count);

    // --- Channel 0: Background Grid ---
    splitter.SetCurrentChannel(drawList, Channel_Background);
    drawList->AddRectFilled(canvasMin, canvasMax, IM_COL32(22, 24, 29, 255));

    // Procedural grid drawing
    {
        const float minorBase = 16.0f;
        const float minorStep = minorBase * canvas.zoom;
        const float majorStep = minorStep * 5.0f;

        float gridAlpha = 1.0f;
        if (canvas.zoom < 0.5f) {
            gridAlpha = (canvas.zoom - canvas.minZoom) / (0.5f - canvas.minZoom);
            if (gridAlpha < 0.0f) gridAlpha = 0.0f;
        }

        ImU32 minorColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.20f, 0.22f, 0.26f, 0.40f * gridAlpha));
        ImU32 majorColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.28f, 0.31f, 0.38f, 0.80f * (0.3f + 0.7f * gridAlpha)));

        float startX = std::fmod(canvas.pan.x, majorStep);
        if (startX < 0.0f) startX += majorStep;
        float startY = std::fmod(canvas.pan.y, majorStep);
        if (startY < 0.0f) startY += majorStep;

        if (minorStep >= 8.0f) {
            float minStartX = std::fmod(canvas.pan.x, minorStep);
            if (minStartX < 0.0f) minStartX += minorStep;
            float minStartY = std::fmod(canvas.pan.y, minorStep);
            if (minStartY < 0.0f) minStartY += minorStep;

            for (float x = canvasMin.x + minStartX; x < canvasMax.x; x += minorStep) {
                drawList->AddLine(ImVec2(x, canvasMin.y), ImVec2(x, canvasMax.y), minorColor, 1.0f);
            }
            for (float y = canvasMin.y + minStartY; y < canvasMax.y; y += minorStep) {
                drawList->AddLine(ImVec2(canvasMin.x, y), ImVec2(canvasMax.x, y), minorColor, 1.0f);
            }
        }

        for (float x = canvasMin.x + startX; x < canvasMax.x; x += majorStep) {
            drawList->AddLine(ImVec2(x, canvasMin.y), ImVec2(x, canvasMax.y), majorColor, 1.5f);
        }
        for (float y = canvasMin.y + startY; y < canvasMax.y; y += majorStep) {
            drawList->AddLine(ImVec2(canvasMin.x, y), ImVec2(canvasMax.x, y), majorColor, 1.5f);
        }
    }

    // Clip draw list to canvas viewport
    drawList->PushClipRect(canvasMin, canvasMax, true);

    // Static animation pulse counter
    static float animPulse = 0.0f;
    animPulse += deltaTime * 4.0f;

    // --- Channel 1: Active connections & Bézier cables ---
    splitter.SetCurrentChannel(drawList, Channel_Wires);
    for (const auto& link : graph.links) {
        const Pin* fromPin = graph.FindPin(link.fromPinId);
        const Pin* toPin = graph.FindPin(link.toPinId);
        if (!fromPin || !toPin) continue;

        const Node* fromNode = graph.FindNode(fromPin->nodeId);
        const Node* toNode = graph.FindNode(toPin->nodeId);
        if (!fromNode || !toNode) continue;

        ImVec2 fromWorld = ImVec2Add(fromNode->worldPos, fromPin->localOffset);
        ImVec2 toWorld = ImVec2Add(toNode->worldPos, toPin->localOffset);

        ImVec2 p1 = canvas.WorldToScreen(fromWorld);
        ImVec2 p4 = canvas.WorldToScreen(toWorld);

        // Horizontal tangents proportional to distance between pins
        float dx = p4.x - p1.x;
        float tangentDist = std::max(std::abs(dx) * 0.5f, 30.0f * canvas.zoom);
        ImVec2 p2(p1.x + tangentDist, p1.y);
        ImVec2 p3(p4.x - tangentDist, p4.y);

        // Color coding dynamically:
        // Bright active green for HIGH (logic 1), muted dark grey for LOW (logic 0)
        bool isHigh = (link.state == LogicValue::High);
        ImU32 wireColor = isHigh ? IM_COL32(40, 240, 80, 255) : IM_COL32(85, 90, 100, 220);
        float wireThickness = isHigh ? (3.0f * canvas.zoom) : (2.0f * canvas.zoom);
        if (wireThickness < 1.5f) wireThickness = 1.5f;

        // Visual pulsing / glow effect when HIGH
        if (isHigh) {
            float pulse = (std::sin(animPulse) + 1.0f) * 0.5f;
            ImU32 glowColor = IM_COL32(50, 255, 100, (int)(40 + 40 * pulse));
            drawList->AddBezierCubic(p1, p2, p3, p4, glowColor, wireThickness + 4.0f * canvas.zoom, 24);
        }

        drawList->AddBezierCubic(p1, p2, p3, p4, wireColor, wireThickness, 24);
    }

    // --- Interactive Hit-Testing for Pins & Nodes ---
    uint32_t hoveredPinId = 0;
    PinKind hoveredPinKind = PinKind::Input;
    ImVec2 hoveredPinScreenPos(0.0f, 0.0f);
    const float pinHitRadius = 9.0f * canvas.zoom;

    // Track if mouse is over any node body
    uint32_t hoveredNodeId = 0;
    ImVec2 mouseWorld = canvas.ScreenToWorld(mousePos);

    // Evaluate nodes in reverse drawOrder for top-most picking
    for (auto it = graph.drawOrder.rbegin(); it != graph.drawOrder.rend(); ++it) {
        Node* node = graph.FindNode(*it);
        if (!node) continue;

        ImVec2 nodeMinWorld = node->worldPos;
        ImVec2 nodeMaxWorld = ImVec2Add(nodeMinWorld, node->size);

        if (mouseWorld.x >= nodeMinWorld.x && mouseWorld.x <= nodeMaxWorld.x &&
            mouseWorld.y >= nodeMinWorld.y && mouseWorld.y <= nodeMaxWorld.y) {
            hoveredNodeId = node->id;
            break;
        }
    }

    // Find hovered pin (circular hit test in screen space)
    for (const auto& node : graph.nodes) {
        auto checkPins = [&](const std::vector<Pin>& pinList) {
            for (const auto& pin : pinList) {
                ImVec2 pinWorld = ImVec2Add(node.worldPos, pin.localOffset);
                ImVec2 pinScreen = canvas.WorldToScreen(pinWorld);
                if (ImVec2DistSq(mousePos, pinScreen) <= (pinHitRadius * pinHitRadius * 1.5f)) {
                    hoveredPinId = pin.id;
                    hoveredPinKind = pin.kind;
                    hoveredPinScreenPos = pinScreen;
                    return;
                }
            }
        };
        checkPins(node.inputs);
        if (hoveredPinId != 0) break;
        checkPins(node.outputs);
        if (hoveredPinId != 0) break;
    }

    // --- 4. Wiring State Machine ---
    if (wiring.state == InteractionState::Idle) {
        if (isCanvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (hoveredPinId != 0) {
                // Pin clicked: if it's an output pin, start dragging wire
                // If it's an input pin that already has a link, detach it and drag wire!
                Pin* pin = graph.FindPin(hoveredPinId);
                if (pin->kind == PinKind::Output) {
                    wiring.state = InteractionState::DraggingWire;
                    wiring.activePinId = hoveredPinId;
                    wiring.wireDragScreenPos = mousePos;
                } else if (pin->kind == PinKind::Input) {
                    // Check if existing incoming link exists
                    for (const auto& link : graph.links) {
                        if (link.toPinId == pin->id) {
                            wiring.state = InteractionState::DraggingWire;
                            wiring.activePinId = link.fromPinId;
                            wiring.wireDragScreenPos = mousePos;
                            graph.RemoveLink(link.id);
                            break;
                        }
                    }
                }
            } else if (hoveredNodeId != 0) {
                // Clicked on node body
                Node* node = graph.FindNode(hoveredNodeId);
                if (!io.KeyCtrl && !node->selected) {
                    for (auto& n : graph.nodes) n.selected = false;
                }
                node->selected = true;
                graph.BringNodeToFront(node->id);
                wiring.state = InteractionState::DraggingNode;
            } else {
                // Clicked on empty canvas -> Marquee selection or clear selection
                if (!io.KeyCtrl) {
                    for (auto& n : graph.nodes) n.selected = false;
                }
                wiring.state = InteractionState::BoxSelecting;
                wiring.boxSelectStartWorld = mouseWorld;
                wiring.boxSelectEndWorld = mouseWorld;
            }
        }
    } else if (wiring.state == InteractionState::DraggingWire) {
        wiring.wireDragScreenPos = mousePos;
        wiring.hoverPinId = 0;

        // Snapping & validation rules
        if (hoveredPinId != 0) {
            Pin* srcPin = graph.FindPin(wiring.activePinId);
            Pin* dstPin = graph.FindPin(hoveredPinId);
            if (srcPin && dstPin && srcPin->nodeId != dstPin->nodeId) {
                // Output can only connect to Input
                if (srcPin->kind == PinKind::Output && dstPin->kind == PinKind::Input) {
                    // Snap wire directly to destination pin terminal
                    wiring.wireDragScreenPos = hoveredPinScreenPos;
                    wiring.hoverPinId = hoveredPinId;
                }
            }
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (wiring.hoverPinId != 0) {
                Pin* srcPin = graph.FindPin(wiring.activePinId);
                Pin* dstPin = graph.FindPin(wiring.hoverPinId);
                if (srcPin && dstPin && srcPin->kind == PinKind::Output && dstPin->kind == PinKind::Input) {
                    // Remove existing connection to input pin if any (an input pin accepts only 1 incoming connection)
                    graph.RemoveLinksConnectedToPin(dstPin->id);

                    // Add validated link
                    Link newLink;
                    newLink.id = graph.AllocateId();
                    newLink.fromPinId = srcPin->id;
                    newLink.toPinId = dstPin->id;
                    newLink.state = srcPin->state;
                    graph.links.push_back(newLink);
                }
            }
            wiring.state = InteractionState::Idle;
            wiring.activePinId = 0;
            wiring.hoverPinId = 0;
        }
    } else if (wiring.state == InteractionState::DraggingNode) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
            ImVec2 deltaWorld = ImVec2(io.MouseDelta.x / canvas.zoom, io.MouseDelta.y / canvas.zoom);
            for (auto& node : graph.nodes) {
                if (node.selected) {
                    node.worldPos = ImVec2Add(node.worldPos, deltaWorld);
                }
            }
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            wiring.state = InteractionState::Idle;
        }
    } else if (wiring.state == InteractionState::BoxSelecting) {
        wiring.boxSelectEndWorld = mouseWorld;

        // Calculate marquee bounding box in world space
        ImVec2 boxMin(std::min(wiring.boxSelectStartWorld.x, wiring.boxSelectEndWorld.x),
                      std::min(wiring.boxSelectStartWorld.y, wiring.boxSelectEndWorld.y));
        ImVec2 boxMax(std::max(wiring.boxSelectStartWorld.x, wiring.boxSelectEndWorld.x),
                      std::max(wiring.boxSelectStartWorld.y, wiring.boxSelectEndWorld.y));

        for (auto& node : graph.nodes) {
            ImVec2 nMin = node.worldPos;
            ImVec2 nMax = ImVec2Add(node.worldPos, node.size);
            bool overlap = !(boxMin.x > nMax.x || boxMax.x < nMin.x ||
                             boxMin.y > nMax.y || boxMax.y < nMin.y);
            if (overlap) {
                node.selected = true;
            }
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            wiring.state = InteractionState::Idle;
        }
    }

    // --- Transient Cable Drawing while Dragging Wire (Channel 1) ---
    if (wiring.state == InteractionState::DraggingWire) {
        Pin* srcPin = graph.FindPin(wiring.activePinId);
        if (srcPin) {
            Node* srcNode = graph.FindNode(srcPin->nodeId);
            if (srcNode) {
                ImVec2 srcWorld = ImVec2Add(srcNode->worldPos, srcPin->localOffset);
                ImVec2 p1 = canvas.WorldToScreen(srcWorld);
                ImVec2 p4 = wiring.wireDragScreenPos;

                float dx = p4.x - p1.x;
                float tangentDist = std::max(std::abs(dx) * 0.5f, 30.0f * canvas.zoom);
                ImVec2 p2(p1.x + tangentDist, p1.y);
                ImVec2 p3(p4.x - tangentDist, p4.y);

                // Transient cable color: Yellow / Orange / Green
                ImU32 cableColor = (wiring.hoverPinId != 0) ? IM_COL32(60, 255, 100, 255) : IM_COL32(245, 180, 45, 230);
                drawList->AddBezierCubic(p1, p2, p3, p4, cableColor, 2.5f * canvas.zoom, 24);
            }
        }
    }

    // --- Channel 0: Marquee Box Selection Drawing ---
    if (wiring.state == InteractionState::BoxSelecting) {
        ImVec2 s0 = canvas.WorldToScreen(wiring.boxSelectStartWorld);
        ImVec2 s1 = canvas.WorldToScreen(wiring.boxSelectEndWorld);
        ImVec2 bMin(std::min(s0.x, s1.x), std::min(s0.y, s1.y));
        ImVec2 bMax(std::max(s0.x, s1.x), std::max(s0.y, s1.y));

        drawList->AddRectFilled(bMin, bMax, IM_COL32(50, 130, 240, 45), 2.0f);
        drawList->AddRect(bMin, bMax, IM_COL32(90, 160, 255, 200), 2.0f, 0, 1.5f);
    }

    // Helper static gate vector drawers
    auto drawGateIcon = [&](NodeType type, ImVec2 min, ImVec2 max, ImU32 color) {
        switch (type) {
            case NodeType::GateAND: {
                float w = max.x - min.x;
                float h = max.y - min.y;
                float midX = min.x + w * 0.45f;
                drawList->AddLine(ImVec2(min.x, min.y), ImVec2(min.x, max.y), color, 2.0f);
                drawList->AddLine(ImVec2(min.x, min.y), ImVec2(midX, min.y), color, 2.0f);
                drawList->AddLine(ImVec2(min.x, max.y), ImVec2(midX, max.y), color, 2.0f);
                drawList->AddBezierCubic(ImVec2(midX, min.y), ImVec2(max.x, min.y), ImVec2(max.x, max.y), ImVec2(midX, max.y), color, 2.0f, 16);
                break;
            }
            case NodeType::GateOR: {
                float w = max.x - min.x;
                float h = max.y - min.y;
                drawList->AddBezierQuadratic(ImVec2(min.x, min.y), ImVec2(min.x + w * 0.25f, min.y + h * 0.5f), ImVec2(min.x, max.y), color, 2.0f, 12);
                ImVec2 tTip(max.x, min.y + h * 0.5f);
                drawList->AddBezierQuadratic(ImVec2(min.x, min.y), ImVec2(min.x + w * 0.55f, min.y), tTip, color, 2.0f, 12);
                drawList->AddBezierQuadratic(ImVec2(min.x, max.y), ImVec2(min.x + w * 0.55f, max.y), tTip, color, 2.0f, 12);
                break;
            }
            case NodeType::GateNOT: {
                float h = max.y - min.y;
                float bubbleR = 4.0f * canvas.zoom;
                drawList->AddTriangle(ImVec2(min.x, min.y), ImVec2(min.x, max.y), ImVec2(max.x - bubbleR * 2.0f, min.y + h * 0.5f), color, 2.0f);
                drawList->AddCircle(ImVec2(max.x - bubbleR, min.y + h * 0.5f), bubbleR, color, 12, 1.8f);
                break;
            }
            case NodeType::GateXOR: {
                float w = max.x - min.x;
                float h = max.y - min.y;
                drawList->AddBezierQuadratic(ImVec2(min.x - w * 0.12f, min.y), ImVec2(min.x + w * 0.13f, min.y + h * 0.5f), ImVec2(min.x - w * 0.12f, max.y), color, 2.0f, 12);
                drawList->AddBezierQuadratic(ImVec2(min.x, min.y), ImVec2(min.x + w * 0.25f, min.y + h * 0.5f), ImVec2(min.x, max.y), color, 2.0f, 12);
                ImVec2 tTip(max.x, min.y + h * 0.5f);
                drawList->AddBezierQuadratic(ImVec2(min.x, min.y), ImVec2(min.x + w * 0.55f, min.y), tTip, color, 2.0f, 12);
                drawList->AddBezierQuadratic(ImVec2(min.x, max.y), ImVec2(min.x + w * 0.55f, max.y), tTip, color, 2.0f, 12);
                break;
            }
            case NodeType::GateNAND: {
                float w = max.x - min.x;
                float h = max.y - min.y;
                float bubbleR = 4.0f * canvas.zoom;
                float andMaxX = max.x - bubbleR * 2.0f;
                float midX = min.x + (andMaxX - min.x) * 0.45f;
                drawList->AddLine(ImVec2(min.x, min.y), ImVec2(min.x, max.y), color, 2.0f);
                drawList->AddLine(ImVec2(min.x, min.y), ImVec2(midX, min.y), color, 2.0f);
                drawList->AddLine(ImVec2(min.x, max.y), ImVec2(midX, max.y), color, 2.0f);
                drawList->AddBezierCubic(ImVec2(midX, min.y), ImVec2(andMaxX, min.y), ImVec2(andMaxX, max.y), ImVec2(midX, max.y), color, 2.0f, 16);
                drawList->AddCircle(ImVec2(max.x - bubbleR, min.y + h * 0.5f), bubbleR, color, 12, 1.8f);
                break;
            }
            default: break;
        }
    };

    // --- Render Nodes in drawOrder (back to front) ---
    for (uint32_t nodeId : graph.drawOrder) {
        Node* node = graph.FindNode(nodeId);
        if (!node) continue;

        ImGui::PushID((int)node->id);

        ImVec2 nodeScreenMin = canvas.WorldToScreen(node->worldPos);
        ImVec2 nodeScreenSize = ImVec2(node->size.x * canvas.zoom, node->size.y * canvas.zoom);
        ImVec2 nodeScreenMax = ImVec2Add(nodeScreenMin, nodeScreenSize);

        // Header height scaled
        float headerHeight = 24.0f * canvas.zoom;
        ImVec2 headerMax = ImVec2(nodeScreenMax.x, nodeScreenMin.y + headerHeight);

        // --- Channel 2: Node bodies, borders, and custom logic gate vector iconography ---
        splitter.SetCurrentChannel(drawList, Channel_NodeBodies);

        // Body background
        ImU32 bodyBg = IM_COL32(33, 36, 44, 235);
        drawList->AddRectFilled(nodeScreenMin, nodeScreenMax, bodyBg, 6.0f * canvas.zoom);

        // Header background (differentiated by node category)
        ImU32 headerCol = IM_COL32(45, 52, 65, 255);
        if (node->type == NodeType::InputToggle || node->type == NodeType::Clock) {
            headerCol = IM_COL32(35, 70, 110, 255);
        } else if (node->type == NodeType::OutputLED || node->type == NodeType::Display7Segment) {
            headerCol = IM_COL32(110, 45, 50, 255);
        } else {
            headerCol = IM_COL32(45, 85, 75, 255);
        }
        drawList->AddRectFilled(nodeScreenMin, headerMax, headerCol, 6.0f * canvas.zoom, ImDrawFlags_RoundCornersTop);

        // Node border (golden/cyan selection glow if selected)
        if (node->selected) {
            drawList->AddRect(nodeScreenMin, nodeScreenMax, IM_COL32(255, 195, 45, 255), 6.0f * canvas.zoom, 0, 2.5f);
        } else {
            drawList->AddRect(nodeScreenMin, nodeScreenMax, IM_COL32(65, 72, 85, 200), 6.0f * canvas.zoom, 0, 1.2f);
        }

        // Title text in header
        if (canvas.zoom >= 0.5f) {
            ImVec2 titlePos(nodeScreenMin.x + 8.0f * canvas.zoom, nodeScreenMin.y + 4.0f * canvas.zoom);
            drawList->AddText(titlePos, IM_COL32(230, 235, 245, 255), node->title.c_str());
        }

        // Custom Vector Gate Iconography inside node center
        if (node->type >= NodeType::GateAND && node->type <= NodeType::GateNAND) {
            float iconPadX = 18.0f * canvas.zoom;
            float iconPadY = 6.0f * canvas.zoom;
            ImVec2 iconMin(nodeScreenMin.x + iconPadX, nodeScreenMin.y + headerHeight + iconPadY);
            ImVec2 iconMax(nodeScreenMax.x - iconPadX, nodeScreenMax.y - iconPadY);
            drawGateIcon(node->type, iconMin, iconMax, IM_COL32(190, 215, 240, 220));
        }

        // --- Channel 3: Pin terminals, hit-test halos, and inner interactive widgets ---
        splitter.SetCurrentChannel(drawList, Channel_PinsWidgets);

        // Render interactive widgets inside nodes
        if (node->type == NodeType::InputToggle) {
            ImVec2 btnPos(nodeScreenMin.x + 12.0f * canvas.zoom, nodeScreenMin.y + headerHeight + 8.0f * canvas.zoom);
            ImVec2 btnSize(nodeScreenSize.x - 36.0f * canvas.zoom, nodeScreenSize.y - headerHeight - 16.0f * canvas.zoom);

            ImGui::SetCursorScreenPos(btnPos);
            ImGui::PushStyleColor(ImGuiCol_Button, node->toggleState ? ImVec4(0.18f, 0.75f, 0.32f, 1.0f) : ImVec4(0.25f, 0.28f, 0.34f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, node->toggleState ? ImVec4(0.25f, 0.85f, 0.40f, 1.0f) : ImVec4(0.35f, 0.38f, 0.45f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.60f, 0.25f, 1.0f));

            char btnLabel[32];
            snprintf(btnLabel, sizeof(btnLabel), "%s##Tgl", node->toggleState ? "HIGH [1]" : "LOW [0]");
            if (ImGui::Button(btnLabel, btnSize)) {
                node->toggleState = !node->toggleState;
            }
            ImGui::PopStyleColor(3);

        } else if (node->type == NodeType::Clock) {
            ImVec2 sliderPos(nodeScreenMin.x + 8.0f * canvas.zoom, nodeScreenMin.y + headerHeight + 6.0f * canvas.zoom);
            ImGui::SetCursorScreenPos(sliderPos);
            ImGui::SetNextItemWidth(nodeScreenSize.x - 32.0f * canvas.zoom);
            ImGui::SliderFloat("##Hz", &node->clockFrequencyHz, 0.2f, 10.0f, "%.1f Hz");

            // Small indicator LED
            ImVec2 ledPos(nodeScreenMin.x + 18.0f * canvas.zoom, nodeScreenMax.y - 14.0f * canvas.zoom);
            bool clkHigh = (!node->outputs.empty() && node->outputs[0].state == LogicValue::High);
            drawList->AddCircleFilled(ledPos, 5.0f * canvas.zoom, clkHigh ? IM_COL32(40, 240, 80, 255) : IM_COL32(30, 45, 35, 255));

        } else if (node->type == NodeType::OutputLED) {
            ImVec2 ledCenter(nodeScreenMin.x + nodeScreenSize.x * 0.5f, nodeScreenMin.y + headerHeight + (nodeScreenSize.y - headerHeight) * 0.5f);
            bool isLit = (!node->inputs.empty() && node->inputs[0].state == LogicValue::High);
            float ledR = 14.0f * canvas.zoom;

            // Glow & bulb
            if (isLit) {
                for (int i = 3; i >= 1; --i) {
                    float glowR = ledR + (float)i * 4.0f * canvas.zoom;
                    drawList->AddCircleFilled(ledCenter, glowR, IM_COL32(50, 255, 80, 50 / i), 24);
                }
            }
            drawList->AddCircleFilled(ledCenter, ledR, isLit ? IM_COL32(50, 255, 80, 250) : IM_COL32(35, 50, 40, 220), 20);
            drawList->AddCircle(ledCenter, ledR, isLit ? IM_COL32(180, 255, 190, 255) : IM_COL32(80, 95, 85, 255), 20, 2.0f);

        } else if (node->type == NodeType::Display7Segment) {
            float padX = 24.0f * canvas.zoom;
            float padY = 8.0f * canvas.zoom;
            ImVec2 dMin(nodeScreenMin.x + padX, nodeScreenMin.y + headerHeight + padY);
            ImVec2 dMax(nodeScreenMax.x - padX, nodeScreenMax.y - padY);

            // Background display pocket
            drawList->AddRectFilled(dMin, dMax, IM_COL32(15, 15, 20, 255), 4.0f);

            // 7 Segment vector elements
            float segW = dMax.x - dMin.x;
            float segH = dMax.y - dMin.y;
            float segThick = segW * 0.16f;
            uint8_t mask = node->segmentMask;

            auto drawSeg = [&](int bit, ImVec2 p0, ImVec2 p1) {
                bool on = (mask & (1 << bit)) != 0;
                ImU32 col = on ? IM_COL32(255, 45, 45, 245) : IM_COL32(45, 20, 20, 90);
                if (on) {
                    drawList->AddRectFilled(ImVec2(p0.x - 2, p0.y - 2), ImVec2(p1.x + 2, p1.y + 2), IM_COL32(255, 40, 40, 40), 2.0f);
                }
                drawList->AddRectFilled(p0, p1, col, 2.0f);
            };

            drawSeg(0, ImVec2(dMin.x + segThick, dMin.y), ImVec2(dMax.x - segThick, dMin.y + segThick));
            drawSeg(1, ImVec2(dMax.x - segThick, dMin.y + segThick), ImVec2(dMax.x, dMin.y + segH * 0.5f));
            drawSeg(2, ImVec2(dMax.x - segThick, dMin.y + segH * 0.5f), ImVec2(dMax.x, dMax.y - segThick));
            drawSeg(3, ImVec2(dMin.x + segThick, dMax.y - segThick), ImVec2(dMax.x - segThick, dMax.y));
            drawSeg(4, ImVec2(dMin.x, dMin.y + segH * 0.5f), ImVec2(dMin.x + segThick, dMax.y - segThick));
            drawSeg(5, ImVec2(dMin.x, dMin.y + segThick), ImVec2(dMin.x + segThick, dMin.y + segH * 0.5f));
            drawSeg(6, ImVec2(dMin.x + segThick, dMin.y + segH * 0.5f - segThick * 0.5f), ImVec2(dMax.x - segThick, dMin.y + segH * 0.5f + segThick * 0.5f));
        }

        // Draw Pin Terminals & Labels
        auto drawPins = [&](const std::vector<Pin>& pinList) {
            for (const auto& pin : pinList) {
                ImVec2 pinWorld = ImVec2Add(node->worldPos, pin.localOffset);
                ImVec2 pinCenter = canvas.WorldToScreen(pinWorld);
                float radius = 5.0f * canvas.zoom;

                bool isHigh = (pin.state == LogicValue::High);
                ImU32 pinFill = isHigh ? IM_COL32(40, 240, 80, 255) : IM_COL32(75, 80, 92, 255);
                ImU32 pinBorder = IM_COL32(200, 210, 225, 240);

                // Pin terminal circle
                drawList->AddCircleFilled(pinCenter, radius, pinFill, 12);
                drawList->AddCircle(pinCenter, radius, pinBorder, 12, 1.5f);

                // Hover snap halo or wiring candidate halo
                if (hoveredPinId == pin.id || wiring.hoverPinId == pin.id) {
                    drawList->AddCircle(pinCenter, radius + 4.0f * canvas.zoom, IM_COL32(255, 220, 60, 220), 16, 2.0f);
                }

                // Pin label text
                if (canvas.zoom >= 0.75f && !pin.name.empty()) {
                    ImVec2 textSize = ImGui::CalcTextSize(pin.name.c_str());
                    ImVec2 labelPos;
                    if (pin.kind == PinKind::Input) {
                        labelPos = ImVec2(pinCenter.x + radius + 3.0f, pinCenter.y - textSize.y * 0.5f);
                    } else {
                        labelPos = ImVec2(pinCenter.x - radius - 3.0f - textSize.x, pinCenter.y - textSize.y * 0.5f);
                    }
                    drawList->AddText(labelPos, IM_COL32(180, 190, 205, 200), pin.name.c_str());
                }
            }
        };

        drawPins(node->inputs);
        drawPins(node->outputs);

        ImGui::PopID();
    }

    // Merge all channels in explicit order (0 -> 1 -> 2 -> 3)
    splitter.Merge(drawList);
    drawList->PopClipRect();

    // --- Right-Click Context Menu for Adding Nodes & Controls ---
    if (ImGui::BeginPopupContextWindow("CircuitCanvasContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        ImVec2 spawnWorld = canvas.ScreenToWorld(io.MousePos);

        ImGui::TextDisabled("Add Logic Component");
        ImGui::Separator();

        if (ImGui::BeginMenu("Inputs")) {
            if (ImGui::MenuItem("Toggle Switch")) {
                graph.AddToggleNode(spawnWorld);
            }
            if (ImGui::MenuItem("Clock Generator (2.0 Hz)")) {
                graph.AddClockNode(spawnWorld, 2.0f);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Logic Gates")) {
            if (ImGui::MenuItem("AND Gate"))  graph.AddGateNode(NodeType::GateAND, spawnWorld);
            if (ImGui::MenuItem("OR Gate"))   graph.AddGateNode(NodeType::GateOR, spawnWorld);
            if (ImGui::MenuItem("NOT Gate"))  graph.AddGateNode(NodeType::GateNOT, spawnWorld);
            if (ImGui::MenuItem("XOR Gate"))  graph.AddGateNode(NodeType::GateXOR, spawnWorld);
            if (ImGui::MenuItem("NAND Gate")) graph.AddGateNode(NodeType::GateNAND, spawnWorld);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Outputs")) {
            if (ImGui::MenuItem("Logic Probe (LED)")) {
                graph.AddOutputLEDNode(spawnWorld);
            }
            if (ImGui::MenuItem("7-Segment Display (BCD)")) {
                graph.Add7SegmentNode(spawnWorld);
            }
            ImGui::EndMenu();
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Reset View (Pan & Zoom)")) {
            canvas.pan = ImVec2(50.0f, 50.0f);
            canvas.zoom = 1.0f;
        }

        // Delete selected items
        bool anySelected = false;
        for (const auto& n : graph.nodes) {
            if (n.selected) { anySelected = true; break; }
        }
        if (anySelected && ImGui::MenuItem("Delete Selected Nodes")) {
            std::vector<uint32_t> toDelete;
            for (const auto& n : graph.nodes) {
                if (n.selected) toDelete.push_back(n.id);
            }
            for (uint32_t nid : toDelete) graph.RemoveNode(nid);
        }

        ImGui::EndPopup();
    }

    // Keyboard shortcuts (Delete key removes selected nodes)
    if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
        std::vector<uint32_t> toDelete;
        for (const auto& n : graph.nodes) {
            if (n.selected) toDelete.push_back(n.id);
        }
        for (uint32_t nid : toDelete) graph.RemoveNode(nid);
    }
}

} // namespace LogicSim
