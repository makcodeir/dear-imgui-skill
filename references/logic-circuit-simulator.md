# Pure Dear ImGui Digital Logic Circuit Simulator Reference

Architecture guide for designing interactive digital logic and dataflow graph simulators in pure Dear ImGui with **zero third-party node libraries** (`imnodes`, `imgui-node-editor`, etc.).

---

## 1. Core Architecture

Keep simulation, layout, and interaction strictly separated into 3 tiers:

```
┌────────────────────────────────────────────────────────┐
│                   CircuitGraph (Model)                 │
│  - Nodes: ID, WorldPos, Size, Inputs/Outputs           │
│  - Links: ID, FromPinID (Output), ToPinID (Input)      │
│  - Decoupled state evaluation: LogicEngine::Step()     │
└───────────────────────────┬────────────────────────────┘
                            │
┌───────────────────────────▼────────────────────────────┐
│                  CanvasState (Transform)               │
│  - Pan & Zoom invariant math                           │
│  - WorldToScreen() / ScreenToWorld() coordinate bridge │
└───────────────────────────┬────────────────────────────┘
                            │
┌───────────────────────────▼────────────────────────────┐
│               CircuitEditor (GUI & Renderer)           │
│  - 4-Channel ImDrawListSplitter                        │
│  - Cubic Bézier routing with horizontal tangents       │
│  - Vector gate shapes (AND, OR, NOT, XOR, NAND)        │
│  - Wiring & Marquee selection state machine            │
└────────────────────────────────────────────────────────┘
```

---

## 2. Canvas Transformation Engine

Store all node coordinates in **World Space**. Transform to **Screen Space** only at render/hit-test boundaries:

```cpp
inline ImVec2 WorldToScreen(const ImVec2& worldPos) const {
    return ImVec2(screenOrigin.x + pan.x + (worldPos.x * zoom),
                  screenOrigin.y + pan.y + (worldPos.y * zoom));
}

inline ImVec2 ScreenToWorld(const ImVec2& screenPos) const {
    return ImVec2((screenPos.x - screenOrigin.x - pan.x) / zoom,
                  (screenPos.y - screenOrigin.y - pan.y) / zoom);
}
```

### Cursor-Centered Zoom Invariant
To prevent the canvas from drifting toward the origin when zooming with the mouse wheel:
```cpp
if (hovered && io.MouseWheel != 0.0f) {
    ImVec2 mouseWorldBefore = canvas.ScreenToWorld(io.MousePos);
    float zoomFactor = (io.MouseWheel > 0.0f) ? 1.15f : (1.0f / 1.15f);
    canvas.zoom = std::clamp(canvas.zoom * zoomFactor, canvas.minZoom, canvas.maxZoom);
    ImVec2 mouseWorldAfter = canvas.ScreenToWorld(io.MousePos);
    
    // Adjust pan offset by the delta in scaled space
    canvas.pan.x += (mouseWorldAfter.x - mouseWorldBefore.x) * canvas.zoom;
    canvas.pan.y += (mouseWorldAfter.y - mouseWorldBefore.y) * canvas.zoom;
}
```

---

## 3. Rendering Layers with `ImDrawListSplitter`

Prevent wires from clipping across adjacent nodes and keep interactive widgets responsive by explicitly partitioning the draw list into 4 distinct depth channels:

| Channel | Content | Description |
|---|---|---|
| **0** | Background | Infinite procedural minor/major grid lines & marquee selection rectangle |
| **1** | Connections | Cubic Bézier link cables, dynamic signal glow, transient dragging wire |
| **2** | Node Bodies | Rounded body cards, headers, borders, custom vector logic gate iconography |
| **3** | Pins & Controls | Terminal circles, hover snap halos, interactive toggles, clock sliders |

```cpp
ImDrawList* drawList = ImGui::GetWindowDrawList();
ImDrawListSplitter splitter;
splitter.Split(drawList, 4);

// 1. Channel 0: Grid
splitter.SetCurrentChannel(drawList, 0);
DrawBackgroundGrid(drawList);

// 2. Channel 1: Connections
splitter.SetCurrentChannel(drawList, 1);
DrawCablesAndWires(drawList);

// 3. Channel 2: Bodies & Logic Shapes
splitter.SetCurrentChannel(drawList, 2);
DrawNodeBodies(drawList);

// 4. Channel 3: Pins & Inner Controls
splitter.SetCurrentChannel(drawList, 3);
DrawPinTerminalsAndWidgets(drawList);

splitter.Merge(drawList);
```

---

## 4. Vector Logic Gate Iconography (`ImDrawList`)

Never use raster textures or generic plain text boxes for logic gates. Draw crisp vector geometry scaled to the node's dimensions:

- **AND Gate**: Flat left boundary combined with a right semicircular arc (`drawList->PathArcTo`).
- **OR Gate**: Inward curved concave input backplate + dual outer convex arcs converging at an acute output peak.
- **NOT Gate**: Triangular buffer with an inverted circle bubble at the right vertex.
- **XOR Gate**: Dual curved input boundary (an offset input curve + OR gate body).
- **NAND Gate**: Flat-backed AND gate silhouette terminating in an inversion bubble.
- **LED Probe**: Multi-layer concentric circles with radial alpha fading for high-intensity glow.

---

## 5. Bézier Cable Routing & Tangents

Render cables using horizontal cubic Bézier curves. Tangent length is proportional to horizontal delta:

$$\text{Tangents} = (\Delta X \times 0.5f, 0.0f)$$

```cpp
ImVec2 p1 = fromScreenPos;
ImVec2 p4 = toScreenPos;
float dx = std::abs(p4.x - p1.x) * 0.5f;
if (dx < 30.0f) dx = 30.0f; // Prevent collapsed tangents when pins are vertically aligned

ImVec2 p2 = ImVec2(p1.x + dx, p1.y);
ImVec2 p3 = ImVec2(p4.x - dx, p4.y);

ImU32 cableColor = (state == LogicValue::High) ? IM_COL32(40, 255, 60, 255)   // Bright Green
                 : (state == LogicValue::Low)  ? IM_COL32(60, 65, 75, 255)    // Muted Slate
                                               : IM_COL32(220, 160, 40, 255); // Floating/Transient

drawList->AddBezierCubic(p1, p2, p3, p4, cableColor, 2.5f * zoom);
```

---

## 6. Decoupled Simulation Loop

Never link circuit evaluation directly to the rendering framerate. Resolve gate propagation iteratively per tick:

```cpp
void LogicEngine::Step(CircuitGraph& graph, float deltaTime, int maxIterations) {
    // 1. Tick internal clock generators
    for (auto& node : graph.nodes) {
        if (node.type == NodeType::Clock) {
            node.clockTimer += deltaTime;
            float period = 1.0f / node.clockFreqHz;
            bool high = std::fmod(node.clockTimer, period) < (period * 0.5f);
            node.outputs[0].state = high ? LogicValue::High : LogicValue::Low;
        }
    }

    // 2. Multi-depth relaxation loop
    for (int iter = 0; iter < maxIterations; ++iter) {
        bool changed = false;

        // Propagate signals across links
        for (auto& link : graph.links) {
            Pin* src = graph.FindPin(link.fromPinId);
            Pin* dst = graph.FindPin(link.toPinId);
            if (src && dst && dst->state != src->state) {
                dst->state = src->state;
                link.state = src->state;
                changed = true;
            }
        }

        // Evaluate node truth tables
        for (auto& node : graph.nodes) {
            if (EvaluateNode(node)) changed = true;
        }

        if (!changed) break; // Converged
    }
}
```

---

## 7. Working Examples & Templates

- **Reference Documentation**: `references/logic-circuit-simulator.md`
- **Standalone Template Header**: `templates/logic_circuit_template.h`
- **Complete Verified Project**: `templates/LogicCircuitSimulator/` (includes `CircuitData.h`, `CircuitEngine.h/cpp`, `CircuitEditor.h/cpp`, and `main.cpp`)
- **Self-Test & Verification**:
  ```bash
  g++ -std=c++17 -Iimgui -DHEADLESS_SELFTEST templates/LogicCircuitSimulator/main.cpp \
      templates/LogicCircuitSimulator/Logic/CircuitEngine.cpp \
      templates/LogicCircuitSimulator/Logic/CircuitEditor.cpp \
      imgui/imgui*.cpp -o logic_selftest
  ```
