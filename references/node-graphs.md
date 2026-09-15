# Node Graph / Dataflow Editors (flowcharts, n8n-style workflows)

How to build node-based editors in Dear ImGui: flowcharts, dataflow/pipeline
editors, visual scripting (UE Blueprints), and n8n/Zapier-style automation
graphs. Everything here is **public API only** — no `imgui_internal.h`.

Working, compile-verified implementation: `templates/node_graph_editor.cpp`
(a UE-Blueprints-styled editor, 7 nodes / 7 links, GUI + headless self-test).

## Contents
1. The architecture that scales
2. Coordinate spaces: the canvas transform
3. Node layout driven by text metrics
4. Pins and type system
5. Drawing wires (bezier)
6. Interaction model (pan / zoom / drag / connect)
7. ID discipline for repeated nodes
8. Rendering order
9. Pitfalls specific to node editors
10. Extending toward n8n-style workflows

---

## 1. The architecture that scales

Three separable layers. Keep them apart and the editor stays maintainable:

```
  Model            Canvas                 Render/Input
  ─────            ──────                 ────────────
  Node[]           pan, zoom              ImDrawList (wires, node chrome)
  Pin[] per node   ToScreen(graph) ────►  InvisibleButton (hit-testing)
  Link[]           ToGraph(screen)        ImGui widgets (menus, popups)
  Graph.selected
```

- **Model** = pure data. No ImGui calls. Serialisable, testable headless.
- **Canvas** = the only place that knows about pan/zoom. A 2-method struct:
  `ToScreen(graph_pos)` and `ToGraph(screen_pos)`. Every other function works
  in ONE space and converts at the boundary.
- **Render/Input** = draw via `ImDrawList`, hit-test via `InvisibleButton`.

The single most important rule: **store node positions in graph space**, never
screen space. Pan/zoom then becomes a pure view transform and never corrupts
the model.

## 2. Coordinate spaces: the canvas transform

```cpp
struct Canvas
{
    ImVec2 pan;     // screen-px offset of the graph origin
    float  zoom;
    ImVec2 origin;  // screen-space top-left of the canvas rect
    ImVec2 mouse;   // mouse in screen space

    ImVec2 ToScreen(const ImVec2& p) const
    { return ImVec2(origin.x + pan.x + p.x * zoom, origin.y + pan.y + p.y * zoom); }

    ImVec2 ToGraph(const ImVec2& s) const
    { return ImVec2((s.x - origin.x - pan.x) / zoom, (s.y - origin.y - pan.y) / zoom); }
};
```

Anchors to get right:

- `origin = ImGui::GetCursorScreenPos()` at the top of the canvas child — the
  one and only conversion from ImGui layout space to your canvas space.
- Get the canvas rect from `GetContentRegionAvail()`; draw the background with
  `GetWindowDrawList()` over `[origin, origin+avail]`.

**Zoom anchored at the cursor** (so the point under the mouse stays put):

```cpp
if (hovered && io.MouseWheel != 0.0f)
{
    ImVec2 before = canvas.ToGraph(canvas.mouse);
    canvas.zoom = Clampf(zoom * (1.0f + io.MouseWheel * 0.1f), 0.3f, 2.5f);
    ImVec2 after = canvas.ToGraph(canvas.mouse);
    canvas.pan += ImVec2((after.x-before.x) * canvas.zoom, (after.y-before.y) * canvas.zoom);
}
```

Get this wrong and zoom drifts toward the origin — the classic node-editor bug.

## 3. Node layout driven by text metrics

Derive node size from content, not constants, so fonts/DPI changes don't clip:

```cpp
static inline float Clampf(float v, float lo, float hi)
{ return v < lo ? lo : (v > hi ? hi : v); }

static void LayoutNode(Node* n)
{
    int rows = n->input_count > n->output_count ? n->input_count : n->output_count;
    if (rows < 1) rows = 1;
    n->size = ImVec2(kNodeWidth, kHeaderHeight + kBodyPad*2.0f + rows * kPinRowHeight);
}
```

> `ImMax` / `ImMin` / `ImClamp` live in `imgui_internal.h`, not `imgui.h`. If you
> are honouring public-API-only (recommended), write your own `Clampf` /
> `Maxi` helpers as above rather than including the internal header for them.

For fully metric-driven heights use `ImGui::GetTextLineHeightWithSpacing()`
per pin row and `GetFrameHeight()` for the header. Keep widths in font-size
multiples for DPI correctness.

Pin anchor points must be computed from the SAME formula used for layout, so
wires always land on the connector circle:

```cpp
ImVec2 Node_InputPinPos (const Node* n, int i) { return ImVec2(n->pos.x,             n->pos.y + kHeaderHeight + kBodyPad + kPinRowHeight*(i+0.5f)); }
ImVec2 Node_OutputPinPos(const Node* n, int i) { return ImVec2(n->pos.x+n->size.x,   n->pos.y + kHeaderHeight + kBodyPad + kPinRowHeight*(i+0.5f)); }
```

## 4. Pins and type system

Give pins a type and a colour by type — this is what makes a graph readable and
is the core of a dataflow ("type compatibility") model:

```cpp
enum PinKind { Pin_None=0, Pin_Exec, Pin_Float, Pin_Int, Pin_Bool, Pin_String, Pin_Vector };
ImU32 PinKind_Color(PinKind k);  // exec=white, float=green, int=cyan, ...
```

- Exec/flow pins (white) drive control flow (UE, n8n "main").
- Data pins (coloured) carry values; a wire should connect same-kind pins.
- Enforce compatibility at connect time, or colour the wire red when invalid.

## 5. Drawing wires (bezier)

Horizontal-tangent cubic bezier is the readable default:

```cpp
static void DrawWire(ImDrawList* dl, const ImVec2& a, const ImVec2& b, ImU32 col, float th)
{
    float dx = b.x - a.x;
    float c  = (dx < 0.0f ? -dx : dx) * 0.5f;
    if (c < 30.0f) c = 30.0f;               // keep short/backward wires readable
    dl->AddBezierCubic(a, ImVec2(a.x + c, a.y), ImVec2(b.x - c, b.y), b, col, th, 0);
}
```

`num_segments = 0` lets ImGui auto-tessellate. Thickness scales with zoom
(`3.0f * zoom`). Colour the wire by the **source** pin kind.

Note: `ImBezierCubicCalc` is **internal** — do not reach for it. If you need a
point on the curve for hit-testing, sample the cubic yourself or test against a
polyline approximation of the same control points.

## 6. Interaction model

Use one full-canvas `InvisibleButton` for background interactions and one
`InvisibleButton` per node body / per pin:

```cpp
ImGui::SetCursorScreenPos(rect_min);
ImGui::InvisibleButton("##bg", avail,
    ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
bool hovered = ImGui::IsItemHovered();
bool active  = ImGui::IsItemActive();
```

- **Pan**: `IsItemActive()` + `IsMouseDragging(Middle)` → `pan += io.MouseDelta`;
  also left-drag on empty canvas.
- **Node move**: per-node `InvisibleButton`; on `IsItemActive()` +
  `IsMouseDragging(Left)` → `node->pos += io.MouseDelta / zoom`.
- **Wire drag**: click an output pin → remember `(node, pin)`; each frame draw a
  wire from that pin to `io.MousePos`; on `IsMouseReleased(Left)`, connect to the
  node under the cursor.
- **Zoom/context menu**: wheel on hover; `BeginPopupContextItem` for right-click.

Order matters: draw the background `InvisibleButton` **before** nodes, and the
nodes' buttons **after**, so nodes win the hit-test.

## 7. ID discipline for repeated nodes

Every node has widgets with the same labels ("##body", "##in", "##out").
Without scoping, they all collide. Wrap each node's widgets in `PushID(index)`:

```cpp
ImGui::PushID(node_index);
    ImGui::SetCursorScreenPos(p0);
    ImGui::InvisibleButton("##body", size);
    // ...pins: PushID(1000+i) / PushID(2000+i) for input/output pins
ImGui::PopID();
```

This is the node-graph form of the skill's ID rule: a loop body **must** open an
ID scope. A collision here makes one node's pins drive another's — and it
asserts at frame end, not at compile time.

## 8. Rendering order

Draw in this order or nodes/wires will look wrong:

1. Canvas background + grid
2. **Wires** (under the nodes)
3. Nodes: shadow → body → header bar → outline → title/subtitle text
4. Pins (circle + label + hover halo)
5. Live "dragging" wire (on top)
6. Popups / tooltips

Wires must be drawn before node bodies, or they appear to cut across nodes.

## 9. Pitfalls specific to node editors

1. **Storing positions in screen space.** Pan/zoom then rewrites the model every
   frame and drifts. Always graph space + view transform.
2. **Zoom that ignores the mouse anchor** (see §2) — causes drift toward origin.
3. **Forgetting `PushID` per node** — instant ID collisions; assert at frame end.
4. **Hit-test order**: background button submitted after node buttons swallows
   node clicks. Background first, nodes second.
5. **Pin anchors computed differently from layout** — wires miss the connectors.
   Share one formula / one function.
6. **Invisible canvas button with default flags** doesn't accept the right mouse
   buttons — pass `ImGuiButtonFlags_MouseButtonLeft | _MouseButtonMiddle`.
7. **`io.MousePos` is `(-FLT_MAX,-FLT_MAX)` when the mouse is outside the
   window** — a live wire then flies off to a huge coordinate. Guard with
   `ImGui::IsMousePosValid()` before using it.
8. **Naive per-frame allocation** for a 1000-node graph. Reuse arrays / an
   `ImVector`, and only fine for hundreds of nodes; for thousands, add a
   spatial index or quad-tree.
9. **Grid drawn as lines every N px** gets expensive when zoomed out; step a
   dot/label by `grid * zoom` and skip when `grid < ~6px` (see template).

## 10. Extending toward n8n-style workflows

The same skeleton covers automation/workflow editors. Mapping:

| Concept        | Blueprint          | n8n / workflow        | Implementation |
|----------------|--------------------|-----------------------|----------------|
| Node           | Function node      | Action/trigger node   | `Node` struct  |
| Exec wire      | `then` exec pin    | `main` connection     | `Pin_Exec`     |
| Data wire      | typed value pin    | parameter binding     | `Pin_*` typed  |
| Node library   | right-click search | node palette panel    | `BeginListBox` + filter |
| Run status     | (none)             | success/error badge   | per-node `int status` → colour header |
| Sub-graphs     | collapsed graph    | sub-workflow          | node holding a `Graph*` |

For a Kotlin/Java-style flowchart editor, the node with the most inputs is the
"merge" node; draw the branch labels on the wire (`AddText` at the bezier
midpoint) — see the branch-label note below.

**Blueprints-style visual language** (what the template implements):
rounded dark body, coloured header bar per category, C++ function subtitle,
left inputs / right outputs, type-coloured circular connectors, bezier wires.

**n8n-style visual language**: soft-rounded white/grey cards, a header icon,
a single left "input" and right "output" connector, status dot (green = ok,
red = error, grey = idle), and a label under the node. Same model, different
`DrawNode` cosmetics — keep the two concerns separate so you can restyle
without touching the interaction code.

### Branch labels on wires

To label a fork (true/false, if/else):

```cpp
ImVec2 a = canvas.ToScreen(out_pos), b = canvas.ToScreen(in_pos);
DrawWire(dl, a, b, col, 3.0f * zoom);
// Cheap midpoint: the cubic's t=0.5 point.
ImVec2 mid = ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
dl->AddText(mid, IM_COL32(220,220,220,255), "true");
```

## Verification

`templates/node_graph_editor.cpp` builds two ways, both zero-warning under
`-Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Werror`:

```bash
# Headless self-test (no window) — proves model + layout + a full render pass
g++ -std=c++11 -I. -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Werror \
    -DHEADLESS_SELFTEST templates/node_graph_editor.cpp \
    imgui.cpp imgui_demo.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp \
    -o /tmp/nodegraph && /tmp/nodegraph    # prints: blueprint_node_graph OK nodes=7 links=7

# GUI (GLFW + OpenGL3)
g++ -std=c++11 -I. -Ibackends -I<glfw>/include -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Werror \
    templates/node_graph_editor.cpp imgui.cpp imgui_demo.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp \
    backends/imgui_impl_glfw.cpp backends/imgui_impl_opengl3.cpp <glfw>/libglfw3.a -lGL -ldl -lpthread \
    -o /tmp/nodegraph_gui
```
